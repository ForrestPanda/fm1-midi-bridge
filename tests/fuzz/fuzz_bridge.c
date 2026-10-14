/* SPDX-License-Identifier: MIT */
/* The whole bridge task (app/bridge.c) on the simulator in tests/host, driven by a
 * script decoded from the input: each byte picks an operation (TRS bytes, USB
 * packets, a BLE packet, an injected message, a direct send, link state changes,
 * settings changes, panic, clock, recorder, time) and the bridge steps after each.
 * What we look for: memory errors and UB in the glue between the parsers, the
 * router, the rings and the generators, under any order of events. */
#include "bridge.h"
#include "settings.h"
#include "editor.h"
#include "midi/midi_msg.h"
#include "midi/recorder.h"
#include "bridge_sim.h"
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

static int inj_port;
static void inj_cb(void *ctx, const midi_msg *m) { (void)ctx; if (inj_port & 0x80) bridge_send_to(inj_port & 3, m); else bridge_inject(inj_port & 3, m); }

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
    size_t pos = 1;
    int ops = 0;
    sim_reset();
    sim.uart_fail = n > 0 && (d[0] & 1);
    sim.ble_max = n > 0 ? (uint16_t)(5 + (d[0] >> 1)) : 20;
    bridge_init();
    while (pos < n && ops++ < 4096) {
        uint8_t op = d[pos++] % 13, a = pos < n ? d[pos] : 0;
        size_t len, left = n - pos;
        switch (op) {
        case 0: len = a & 63; pos++; if (len > left) len = left; sim_trs_push(d + pos, len); pos += len; break;
        case 1: len = (size_t)(a & 31) * 4; pos++; if (len > left) len = left & ~3u; sim_usb_push(d + pos, (uint32_t)len); pos += len; break;
        case 2: len = (size_t)a * 3; pos++; if (len > left) len = left; sim_ble_push(d + pos, (uint16_t)len); pos += len; break;
        case 3: case 4: {                                   /* inject (3) or send_to (4) parsed from the bytes that follow */
            midi_parser p;
            inj_port = (op == 4 ? 0x80 : 0) | (a & 3); pos++;
            len = pos < n ? d[pos] & 15 : 0; pos++; if (len > left) len = left > 0 ? left - 1 : 0;
            midi_parser_init(&p, inj_cb, NULL);
            midi_parser_feed(&p, d + pos, len); pos += len;
            break;
        }
        case 5: sim.usb_configured = a & 1; sim.ble_ready = (a >> 1) & 1; sim.ble_max = (uint16_t)(5 + (a >> 2) * 4); pos++; break;
        case 6: {
            fm1_settings *s = settings_get();
            if (left >= 6) {
                s->routes = (uint16_t)((d[pos] | d[pos + 1] << 8) & ROUTE_VALID_MASK);
                s->filters = d[pos + 2] & 7; s->merge_guard = d[pos + 3] & 1; s->panic_on_disconnect = (d[pos + 3] >> 1) & 1;
                s->clock_to = d[pos + 4] & 3; s->clock_bpm = (uint16_t)(20 + d[pos + 5]);
                s->input[0].transpose = (int8_t)(d[pos + 1] % 49 - 24); s->input[0].vel_curve = d[pos + 2] % VEL_CURVE_COUNT;
                pos += 6;
            } else { s->routes = 0; pos = n; }
            editor_port_apply();
            break;
        }
        case 7: bridge_panic_request(); break;
        case 8: bridge_clock_run(a & 1); pos++; break;
        case 9: {
            midi_recorder *r = bridge_recorder();
            switch (a & 3) {
            case 0: rec_start(r, sim.ms); break;
            case 1: rec_stop(r, sim.ms); break;
            case 2: rec_play(r, sim.ms, (a >> 2) & 1); break;
            default: rec_play_stop(r); break;
            }
            bridge_recorder_ports((uint8_t)(a >> 3));
            pos++;
            break;
        }
        case 10: sim.ms += a; pos++; break;
        case 11: { int k = a & 7; pos++; while (k--) bridge_step(); break; }
        default: sim.ms += 10; break;
        }
        bridge_step();
    }
    { int k; for (k = 0; k < 40; k++) { bridge_step(); sim.ms += 10; } }   /* drain generators */
    return 0;
}

const uint8_t *fuzz_seed(int i, size_t *n)
{
    static const uint8_t s0[] = {0x00, 0x00, 0x03, 0x90, 0x3C, 0x64, 0x11, 0x02};                     /* TRS note */
    static const uint8_t s1[] = {0x00, 0x01, 0x04, 0x09, 0x90, 0x40, 0x64, 0x05, 0x40, 0x3C, 0x00};     /* USB note */
    static const uint8_t s2[] = {0x00, 0x02, 0x02, 0x80, 0x80, 0x90, 0x41, 0x64, 0x80};                /* BLE note */
    static const uint8_t s3[] = {0x00, 0x01, 0x03, 0x04, 0xF0, 0x7D, 0x46, 0x04, 0x4D, 0x31, 0x01, 0x05, 0xF7, 0x00, 0x00};   /* editor info */
    static const uint8_t s4[] = {0x00, 0x01, 0x04, 0x04, 0xF0, 0x00, 0x32, 0x04, 0x45, 0x00, 0x00, 0x04, 0x00, 0x40, 0x7F, 0x05, 0xF7, 0x00, 0x00};   /* update handshake */
    static const uint8_t s5[] = {0x00, 0x07, 0x0B, 0x07, 0x05, 0x02, 0x0B, 0x07, 0x05, 0x03};           /* panic, link drop */
    static const uint8_t s6[] = {0x00, 0x06, 0x66, 0x00, 0x01, 0x01, 0x01, 0x78, 0x08, 0x01, 0x0A, 0xFF, 0x0A, 0xFF, 0x08, 0x00};   /* settings, clock */
    static const uint8_t s7[] = {0x00, 0x09, 0x00, 0x00, 0x03, 0x90, 0x3C, 0x64, 0x0A, 0x64, 0x09, 0x01, 0x09, 0x0A, 0x0A, 0xFF, 0x0A, 0xFF};   /* record, play */
    static const uint8_t s8[] = {0x01, 0x03, 0x83, 0x03, 0x90, 0x30, 0x50, 0x04, 0x81, 0x03, 0x80, 0x30, 0x00};   /* no UART, inject, send_to */
    switch (i) {
    case 0: *n = sizeof s0; return s0;  case 1: *n = sizeof s1; return s1;  case 2: *n = sizeof s2; return s2;
    case 3: *n = sizeof s3; return s3;  case 4: *n = sizeof s4; return s4;  case 5: *n = sizeof s5; return s5;
    case 6: *n = sizeof s6; return s6;  case 7: *n = sizeof s7; return s7;  case 8: *n = sizeof s8; return s8;
    default: return NULL;
    }
}
