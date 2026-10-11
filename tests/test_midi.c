/* Host tests: parser, BLE MIDI codec, USB-MIDI codec, and round trips. */
#include "../app/midi/midi_msg.h"
#include "../app/midi/blemidi.h"
#include "../app/midi/usbmidi.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* collect messages as a flat byte stream + a list of (kind,flags,len,ts) */
typedef struct { uint8_t bytes[4096]; size_t n; int count; uint8_t kinds[256]; uint8_t flags[256]; uint16_t ts[256]; } sink;
static void sink_cb(void *ctx, const midi_msg *m)
{
    sink *s = ctx;
    memcpy(s->bytes + s->n, m->b, m->len); s->n += m->len;
    s->kinds[s->count] = m->kind; s->flags[s->count] = m->flags; s->count++;
}
static void sink_ts_cb(void *ctx, uint16_t ts, const midi_msg *m)
{
    sink *s = ctx;
    s->ts[s->count] = ts;
    sink_cb(ctx, m);
}

static void test_parser(void)
{
    sink s = {0};
    midi_parser p;
    const uint8_t in[] = {
        0x90, 0x3C, 0x64,            /* note on */
        0x3E, 0x64,                  /* running status */
        0xF8,                        /* clock in between */
        0x3C, 0x00,                  /* running status continues after realtime */
        0xC0, 0x05,                  /* program change, 1 data byte */
        0x06,                        /* running status, 1 byte */
        0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7, /* GM on */
        0x3C, 0x40,                  /* orphan data after SysEx: dropped */
        0xF2, 0x10, 0x20,            /* song position */
        0x10, 0x20,                  /* no running status for system common: dropped */
        0xE0, 0x00, 0x40,            /* pitch bend */
    };
    midi_parser_init(&p, sink_cb, &s);
    midi_parser_feed(&p, in, sizeof in);
    CHECK(s.count == 9);
    CHECK(s.kinds[0] == MIDI_KIND_CHANNEL && s.kinds[2] == MIDI_KIND_REALTIME);
    CHECK(s.kinds[6] == MIDI_KIND_SYSEX && s.flags[6] == (MIDI_SYSEX_START | MIDI_SYSEX_END));
    CHECK(s.kinds[7] == MIDI_KIND_COMMON);
    {
        const uint8_t want[] = {0x90,0x3C,0x64, 0x90,0x3E,0x64, 0xF8, 0x90,0x3C,0x00, 0xC0,0x05, 0xC0,0x06,
                                0xF0,0x7E,0x7F,0x09,0x01,0xF7, 0xF2,0x10,0x20, 0xE0,0x00,0x40};
        CHECK(s.n == sizeof want && memcmp(s.bytes, want, s.n) == 0);
    }
    /* long SysEx in chunks, fed byte by byte */
    memset(&s, 0, sizeof s);
    midi_parser_init(&p, sink_cb, &s);
    {
        uint8_t sx[200]; size_t i;
        sx[0] = 0xF0; for (i = 1; i < 199; i++) sx[i] = (uint8_t)(i & 0x7F); sx[199] = 0xF7;
        for (i = 0; i < 200; i++) midi_parser_feed(&p, sx + i, 1);
        CHECK(s.count == 4);
        CHECK(s.flags[0] == MIDI_SYSEX_START && s.flags[1] == 0 && s.flags[2] == 0 && s.flags[3] == MIDI_SYSEX_END);
        CHECK(s.n == 200 && memcmp(s.bytes, sx, 200) == 0);
    }
}

static void test_blemidi_roundtrip(void)
{
    blemidi_enc e;
    blemidi_dec d;
    sink s = {0};
    midi_msg m;
    uint16_t len;
    const uint8_t *pkt;

    blemidi_enc_init(&e, 20);     /* MTU 23 */
    blemidi_dec_init(&d, sink_ts_cb, &s);

    m.kind = MIDI_KIND_CHANNEL; m.flags = 0; m.len = 3;
    m.b[0] = 0x90; m.b[1] = 0x3C; m.b[2] = 0x64;
    CHECK(blemidi_enc_add(&e, 1000, &m) == 1);
    m.b[1] = 0x40;
    CHECK(blemidi_enc_add(&e, 1000, &m) == 1);       /* running status, same ts */
    m.b[1] = 0x43;
    CHECK(blemidi_enc_add(&e, 1003, &m) == 1);       /* new ts */
    m.kind = MIDI_KIND_REALTIME; m.len = 1; m.b[0] = 0xF8;
    CHECK(blemidi_enc_add(&e, 1003, &m) == 1);
    /* header(1) + ts(1)+3 + 2 + ts(1)+3 + ts(1)+1 = 13 */
    CHECK(e.len == 13);
    m.kind = MIDI_KIND_CHANNEL; m.len = 3; m.b[0] = 0x80; m.b[1] = 0x3C; m.b[2] = 0x00;
    CHECK(blemidi_enc_add(&e, 1010, &m) == 1);       /* 17 */
    CHECK(blemidi_enc_add(&e, 1010, &m) == 1);       /* rs: 19 */
    CHECK(blemidi_enc_add(&e, 1011, &m) == 0);       /* would be 23 > 20 */
    pkt = blemidi_enc_take(&e, &len);
    CHECK(len == 19);
    CHECK((pkt[0] & 0xC0) == 0x80 && (pkt[0] & 0x3F) == ((1000 >> 7) & 0x3F));
    CHECK(blemidi_dec_feed(&d, pkt, len) == 0);
    CHECK(s.count == 6);
    CHECK(s.ts[0] == 1000 && s.ts[1] == 1000 && s.ts[2] == 1003 && s.ts[3] == 1003 && s.ts[4] == 1010 && s.ts[5] == 1010);
    CHECK(s.kinds[3] == MIDI_KIND_REALTIME);
    {
        const uint8_t want[] = {0x90,0x3C,0x64, 0x90,0x40,0x64, 0x90,0x43,0x64, 0xF8, 0x80,0x3C,0x00, 0x80,0x3C,0x00};
        CHECK(s.n == sizeof want && memcmp(s.bytes, want, s.n) == 0);
    }
}

static void test_blemidi_sysex_span(void)
{
    /* a 200-byte SysEx through the parser -> BLE encoder (MTU 23) -> decoder */
    midi_parser p;
    blemidi_enc e;
    blemidi_dec d;
    sink s = {0};
    uint8_t sx[200]; size_t i;
    struct { blemidi_enc *e; blemidi_dec *d; } ctx;
    sx[0] = 0xF0; for (i = 1; i < 199; i++) sx[i] = (uint8_t)(i & 0x7F); sx[199] = 0xF7;

    blemidi_enc_init(&e, 20);
    blemidi_dec_init(&d, sink_ts_cb, &s);
    (void)ctx;
    /* feed parser output into the encoder one message at a time, flushing packets into the decoder */
    {
        /* a tiny adapter: parser callback splits SysEx chunks to fit the packet */
        struct adapter { blemidi_enc *e; blemidi_dec *d; } a = { &e, &d };
        void adapter_cb(void *c, const midi_msg *m);
        midi_parser_init(&p, adapter_cb, &a);
        midi_parser_feed(&p, sx, sizeof sx);
        {
            uint16_t len; const uint8_t *pkt = blemidi_enc_take(&e, &len);
            if (len) CHECK(blemidi_dec_feed(&d, pkt, len) == 0);
        }
    }
    CHECK(s.n == 200 && memcmp(s.bytes, sx, 200) == 0);
    CHECK(s.count >= 4);
    CHECK(s.flags[0] & MIDI_SYSEX_START);
    CHECK(s.flags[s.count - 1] & MIDI_SYSEX_END);
    for (i = 1; i + 1 < (size_t)s.count; i++) CHECK(s.flags[i] == 0);
}

/* splits a SysEx chunk into pieces that fit the open packet, flushing as needed */
void adapter_cb(void *c, const midi_msg *m)
{
    struct adapter { blemidi_enc *e; blemidi_dec *d; } *a = c;
    if (m->kind != MIDI_KIND_SYSEX) {
        if (!blemidi_enc_add(a->e, 5, m)) {
            uint16_t len; const uint8_t *pkt = blemidi_enc_take(a->e, &len);
            blemidi_dec_feed(a->d, pkt, len);
            blemidi_enc_add(a->e, 5, m);
        }
        return;
    }
    {
        uint8_t off = 0;
        while (off < m->len) {
            midi_msg part = *m;
            uint8_t take = (uint8_t)(m->len - off);
            if (take > 8) take = 8;              /* small pieces for the test */
            part.len = take;
            memcpy(part.b, m->b + off, take);
            part.flags = (uint8_t)(((off == 0) ? (m->flags & MIDI_SYSEX_START) : 0) |
                                   ((off + take == m->len) ? (m->flags & MIDI_SYSEX_END) : 0));
            if (blemidi_enc_add(a->e, 5, &part) != 1) {
                uint16_t len; const uint8_t *pkt = blemidi_enc_take(a->e, &len);
                blemidi_dec_feed(a->d, pkt, len);
                if (blemidi_enc_add(a->e, 5, &part) != 1) { printf("FAIL piece too big\n"); fails++; return; }
            }
            off = (uint8_t)(off + take);
        }
    }
}

static void test_blemidi_decode_vectors(void)
{
    /* packets from the spec examples */
    blemidi_dec d;
    sink s = {0};
    const uint8_t p1[] = {0x80, 0x80, 0x90, 0x3C, 0x7F, 0x3E, 0x7F};         /* rs, same ts */
    const uint8_t p2[] = {0x80, 0x80, 0x90, 0x3C, 0x7F, 0x81, 0x3E, 0x7F};   /* new ts, running status */
    const uint8_t p2b[] = {0x80, 0x81, 0x3E, 0x7F};                          /* running status with no status: bad */
    const uint8_t p3[] = {0x80, 0x80, 0xF0, 0x7E, 0x7F};                     /* sysex start, continues */
    const uint8_t p4[] = {0x80, 0x09, 0x01, 0x81, 0xF7};                     /* sysex continuation + end */
    blemidi_dec_init(&d, sink_ts_cb, &s);
    CHECK(blemidi_dec_feed(&d, p1, sizeof p1) == 0 && s.count == 2);
    memset(&s, 0, sizeof s);
    CHECK(blemidi_dec_feed(&d, p2, sizeof p2) == 0 && s.count == 2 && s.ts[0] == 0 && s.ts[1] == 1);
    memset(&s, 0, sizeof s);
    CHECK(blemidi_dec_feed(&d, p2b, sizeof p2b) == -1);
    memset(&s, 0, sizeof s);
    CHECK(blemidi_dec_feed(&d, p3, sizeof p3) == 0 && s.count == 1 && s.flags[0] == MIDI_SYSEX_START);
    CHECK(blemidi_dec_feed(&d, p4, sizeof p4) == 0 && s.count == 2 && s.flags[1] == MIDI_SYSEX_END);
    {
        const uint8_t want[] = {0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7};
        CHECK(s.n == 6 && memcmp(s.bytes, want, 6) == 0);
    }
    /* realtime inside a SysEx */
    memset(&s, 0, sizeof s);
    {
        const uint8_t p5[] = {0x80, 0x80, 0xF0, 0x01, 0x81, 0xF8, 0x02, 0x82, 0xF7};
        CHECK(blemidi_dec_feed(&d, p5, sizeof p5) == 0);
        CHECK(s.count == 3 && s.kinds[1] == MIDI_KIND_REALTIME);
        /* sysex chunk F0 01, realtime F8, chunk 02 F7: 5 bytes in order */
        CHECK(s.n == 5 && s.bytes[2] == 0xF8);
    }
}

static void test_usbmidi(void)
{
    usbmidi_dec d;
    sink s = {0};
    uint8_t out[64];
    midi_msg m;
    uint16_t n;
    usbmidi_dec_init(&d, sink_cb, &s);

    m.kind = MIDI_KIND_CHANNEL; m.flags = 0; m.len = 3; m.b[0] = 0x9F; m.b[1] = 1; m.b[2] = 2;
    n = usbmidi_encode(1, &m, out);
    CHECK(n == 4 && out[0] == 0x19 && out[1] == 0x9F);
    usbmidi_dec_feed(&d, out, n);
    m.len = 2; m.b[0] = 0xC3; m.b[1] = 7;
    n = usbmidi_encode(0, &m, out); CHECK(out[0] == 0x0C);
    usbmidi_dec_feed(&d, out, n);
    m.kind = MIDI_KIND_REALTIME; m.len = 1; m.b[0] = 0xFA;
    n = usbmidi_encode(0, &m, out); CHECK(out[0] == 0x0F);
    usbmidi_dec_feed(&d, out, n);
    m.kind = MIDI_KIND_COMMON; m.len = 1; m.b[0] = 0xF6;
    n = usbmidi_encode(0, &m, out); CHECK(out[0] == 0x05);
    usbmidi_dec_feed(&d, out, n);
    m.kind = MIDI_KIND_COMMON; m.len = 3; m.b[0] = 0xF2; m.b[1] = 1; m.b[2] = 2;
    n = usbmidi_encode(0, &m, out); CHECK(out[0] == 0x03);
    usbmidi_dec_feed(&d, out, n);
    /* sysex of 7 bytes: 4,4(?) -> 3 + 3 + end1 => CIN 4, 4, 5 */
    m.kind = MIDI_KIND_SYSEX; m.flags = MIDI_SYSEX_START | MIDI_SYSEX_END; m.len = 7;
    m.b[0] = 0xF0; m.b[1] = 1; m.b[2] = 2; m.b[3] = 3; m.b[4] = 4; m.b[5] = 5; m.b[6] = 0xF7;
    n = usbmidi_encode(0, &m, out);
    CHECK(n == 12 && out[0] == 0x04 && out[4] == 0x04 && out[8] == 0x05 && out[9] == 0xF7);
    usbmidi_dec_feed(&d, out, n);
    CHECK(s.count == 6 && s.kinds[5] == MIDI_KIND_SYSEX && s.flags[5] == (MIDI_SYSEX_START | MIDI_SYSEX_END));
    {
        const uint8_t want[] = {0x9F,1,2, 0xC3,7, 0xFA, 0xF6, 0xF2,1,2, 0xF0,1,2,3,4,5,0xF7};
        CHECK(s.n == sizeof want && memcmp(s.bytes, want, s.n) == 0);
    }
    /* sysex split in two chunks of 6 then 2 (end): CIN 4,4 then 6 */
    memset(&s, 0, sizeof s);
    m.flags = MIDI_SYSEX_START; m.len = 6; m.b[0] = 0xF0; m.b[1] = 1; m.b[2] = 2; m.b[3] = 3; m.b[4] = 4; m.b[5] = 5;
    n = usbmidi_encode(0, &m, out); CHECK(n == 8);
    usbmidi_dec_feed(&d, out, n);
    m.flags = MIDI_SYSEX_END; m.len = 2; m.b[0] = 6; m.b[1] = 0xF7;
    n = usbmidi_encode(0, &m, out); CHECK(n == 4 && out[0] == 0x06);
    usbmidi_dec_feed(&d, out, n);
    CHECK(s.count == 1 && s.n == 8 && s.flags[0] == (MIDI_SYSEX_START | MIDI_SYSEX_END));
}


/* ---- router ---- */
#include "../app/midi/router.h"
static int rc[MIDI_PORT_COUNT];
static void count_sink(void *ctx, const midi_msg *m) { (void)m; (*(int *)ctx)++; }
static void test_router(void)
{
    midi_router r;
    midi_msg m = {MIDI_KIND_CHANNEL, 0, 3, {0x90, 60, 100}};
    int i;
    router_init(&r, ROUTE_DEFAULT);
    for (i = 0; i < MIDI_PORT_COUNT; i++) { rc[i] = 0; router_set_sink(&r, i, count_sink, &rc[i]); }
    router_feed(&r, MIDI_PORT_TRS, &m);
    CHECK(rc[MIDI_PORT_BLE] == 1 && rc[MIDI_PORT_USB] == 1 && rc[MIDI_PORT_TRS] == 0);
    router_feed(&r, MIDI_PORT_USB, &m);
    CHECK(rc[MIDI_PORT_BLE] == 2 && rc[MIDI_PORT_USB] == 1 && rc[MIDI_PORT_TRS] == 0);  /* USB -> TRS impossible */
    router_feed(&r, MIDI_PORT_BLE, &m);
    CHECK(rc[MIDI_PORT_USB] == 2 && rc[MIDI_PORT_BLE] == 2);
    router_set(&r, MIDI_PORT_TRS, MIDI_PORT_USB, 0);
    router_feed(&r, MIDI_PORT_TRS, &m);
    CHECK(rc[MIDI_PORT_USB] == 2 && rc[MIDI_PORT_BLE] == 3);
    CHECK(router_set(&r, MIDI_PORT_BLE, MIDI_PORT_TRS, 1) == r.routes && !router_enabled(&r, MIDI_PORT_BLE, MIDI_PORT_TRS));
    CHECK(router_set(&r, MIDI_PORT_USB, MIDI_PORT_USB, 1) == r.routes);
    CHECK(r.count_in[MIDI_PORT_TRS] == 2 && r.count_out[MIDI_PORT_BLE] == 3);
}

/* a long SysEx through the byte parser and USB-MIDI, chunk by chunk */
static void test_usb_sysex_long(void)
{
    midi_parser p;
    usbmidi_dec d;
    sink s = {0};
    uint8_t sx[200]; size_t i;
    struct { usbmidi_dec *d; } ctx = { &d };
    void usb_adapter_cb(void *c, const midi_msg *m);
    sx[0] = 0xF0; for (i = 1; i < 199; i++) sx[i] = (uint8_t)(i & 0x7F); sx[199] = 0xF7;
    usbmidi_dec_init(&d, sink_cb, &s);
    midi_parser_init(&p, usb_adapter_cb, &ctx);
    midi_parser_feed(&p, sx, sizeof sx);
    CHECK(s.n == 200 && memcmp(s.bytes, sx, 200) == 0);
    CHECK(s.count >= 1 && (s.flags[0] & MIDI_SYSEX_START) && (s.flags[s.count - 1] & MIDI_SYSEX_END));
}
void usb_adapter_cb(void *c, const midi_msg *m)
{
    struct { usbmidi_dec *d; } *a = c;
    uint8_t out[(MIDI_SYSEX_CHUNK / 3 + 1) * 4];
    uint16_t n = usbmidi_encode(0, m, out);
    CHECK(m->kind != MIDI_KIND_SYSEX || (m->flags & MIDI_SYSEX_END) || n == (m->len / 3) * 4);
    usbmidi_dec_feed(a->d, out, n);
}

#include "../app/midi/panic.h"
static void test_filters_and_panic(void)
{
    midi_router r; midi_msg m; midi_panic p; int i, n = 0;
    router_init(&r, ROUTE_DEFAULT);
    rc[MIDI_PORT_USB] = 0; router_set_sink(&r, MIDI_PORT_USB, count_sink, &rc[MIDI_PORT_USB]);
    r.filters = FILTER_DROP_CLOCK | FILTER_DROP_SYSEX;
    m.kind = MIDI_KIND_REALTIME; m.flags = 0; m.len = 1; m.b[0] = 0xF8; router_feed(&r, MIDI_PORT_TRS, &m);
    m.b[0] = 0xFE; router_feed(&r, MIDI_PORT_TRS, &m);                       /* not filtered */
    m.kind = MIDI_KIND_SYSEX; m.flags = MIDI_SYSEX_START | MIDI_SYSEX_END; m.len = 2; m.b[0] = 0xF0; m.b[1] = 0xF7;
    router_feed(&r, MIDI_PORT_TRS, &m);
    CHECK(rc[MIDI_PORT_USB] == 1 && r.count_dropped == 2 && r.count_in[MIDI_PORT_TRS] == 1);
    midi_panic_start(&p);
    CHECK(midi_panic_active(&p));
    for (i = 0; midi_panic_next(&p, &m); i++) {
        n++;
        if (i == 0) CHECK(m.b[0] == 0xB0 && m.b[1] == 120 && m.b[2] == 0);
        if (i == 1) CHECK(m.b[0] == 0xB0 && m.b[1] == 123);
        if (i == 31) CHECK(m.b[0] == 0xBF && m.b[1] == 123);
        if (i == 32) CHECK(m.b[0] == 0x80 && m.b[1] == 0 && m.len == 3);
        if (i == (int)MIDI_PANIC_COUNT - 1) CHECK(m.b[0] == 0x8F && m.b[1] == 127);
    }
    CHECK(n == (int)MIDI_PANIC_COUNT && !midi_panic_active(&p));
}

int main(void)
{
    test_parser();
    test_blemidi_roundtrip();
    test_blemidi_sysex_span();
    test_blemidi_decode_vectors();
    test_usbmidi();
    test_router();
    test_usb_sysex_long();
    test_filters_and_panic();
    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("all tests passed\n");
    return 0;
}
