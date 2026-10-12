/* SPDX-License-Identifier: MIT */
/* Every byte-stream parser the bridge task runs, fed the same bytes: the serial
 * MIDI parser, the USB-MIDI event decoder and the BLE-MIDI packet decoder; each
 * message then goes through the router, the velocity/transposition options, the
 * collision guard, the statistics, the decoder and both encoders. */
#include "../../app/midi/midi_msg.h"
#include "../../app/midi/usbmidi.h"
#include "../../app/midi/blemidi.h"
#include "../../app/midi/router.h"
#include "../../app/midi/decode.h"
#include "../../app/midi/stats.h"
#include "../../app/midi/merge.h"
#include "../../app/midi/recorder.h"
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

static midi_router router;
static blemidi_enc enc;
static midi_merge merge;
static midi_stats stats;
static midi_recorder rec;
static uint32_t now;

static void sink(void *ctx, const midi_msg *m)
{
    uint8_t pk[(MIDI_SYSEX_CHUNK / 3 + 1) * 4];
    char txt[48];
    midi_msg out[2];
    int k, i;
    (void)ctx;
    usbmidi_encode(0, m, pk);
    midi_decode(m, txt, sizeof txt);
    stats_add(&stats, m, now);
    rec_add(&rec, m, now);
    k = merge_filter(&merge, m, out);
    for (i = 0; i < k; i++) {
        if (!blemidi_enc_add(&enc, now, &out[i])) { uint16_t len; (void)blemidi_enc_take(&enc, &len); blemidi_enc_add(&enc, now, &out[i]); }
    }
}

static void from_port(void *ctx, const midi_msg *m) { router_feed(&router, (int)(intptr_t)ctx, m); }
static void from_ble(void *ctx, uint16_t ts, const midi_msg *m) { (void)ts; router_feed(&router, (int)(intptr_t)ctx, m); }

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t n)
{
    midi_parser p; usbmidi_dec ud; blemidi_dec bd;
    uint16_t len;
    size_t i;
    router_init(&router, ROUTE_VALID_MASK);
    for (i = 0; i < 16; i++) { router.opt[i].channels = 0xFFFF; router.opt[i].remap = (uint8_t)(n % 17); router.opt[i].flags = (uint8_t)n; }
    router.in[MIDI_PORT_TRS].transpose = (int8_t)(n & 1 ? 12 : -12);
    router.in[MIDI_PORT_TRS].vel_curve = (uint8_t)(n % 4);
    router.in[MIDI_PORT_TRS].vel_fixed = 100;
    router.filters = (uint8_t)(n >> 3);
    router_set_sink(&router, MIDI_PORT_USB, sink, NULL);
    router_set_sink(&router, MIDI_PORT_BLE, sink, NULL);
    blemidi_enc_init(&enc, (uint16_t)(20 + n % 200));
    merge_init(&merge); stats_reset(&stats); rec_init(&rec); rec_start(&rec, 0);
    now = (uint32_t)n * 7u;
    midi_parser_init(&p, from_port, (void *)(intptr_t)MIDI_PORT_TRS);
    midi_parser_feed(&p, data, n);
    usbmidi_dec_init(&ud, from_port, (void *)(intptr_t)MIDI_PORT_USB);
    usbmidi_dec_feed(&ud, data, n);
    blemidi_dec_init(&bd, from_ble, (void *)(intptr_t)MIDI_PORT_BLE);
    for (i = 0; i < n; i += 64) blemidi_dec_feed(&bd, data + i, (uint16_t)(n - i > 64 ? 64 : n - i));
    while (!blemidi_enc_empty(&enc)) (void)blemidi_enc_take(&enc, &len);
    rec_stop(&rec, now + 1000);
    rec_play(&rec, now, 1);
    { midi_msg m; int guard = 4096; while (guard-- && rec_next(&rec, now + 2000, &m)); }
    return 0;
}

const uint8_t *fuzz_seed(int i, size_t *n)
{
    static const uint8_t s0[] = {0x90, 60, 100, 0x80, 60, 0, 0xB0, 7, 100, 0xC0, 5, 0xF8, 0xFE, 0xE0, 0, 64};
    static const uint8_t s1[] = {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7, 0x90, 0x40, 0x7F, 0xF0, 0x00, 0x32, 0x45, 0x01, 0x02, 0x03, 0xF7};
    static const uint8_t s2[] = {0x09, 0x90, 60, 100, 0x08, 0x80, 60, 0, 0x04, 0xF0, 0x7E, 0x7F, 0x07, 0x06, 0x01, 0xF7, 0x0F, 0xF8, 0, 0};
    static const uint8_t s3[] = {0x80, 0x80, 0x90, 60, 100, 0x81, 0x80, 60, 0, 0x80, 0xF0, 0x7E, 0x7F, 0x80, 0xF7};
    switch (i) {
    case 0: *n = sizeof s0; return s0;
    case 1: *n = sizeof s1; return s1;
    case 2: *n = sizeof s2; return s2;
    case 3: *n = sizeof s3; return s3;
    default: return NULL;
    }
}
