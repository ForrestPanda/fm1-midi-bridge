/* SPDX-License-Identifier: MIT */
#include "usbmidi.h"
#include <string.h>

uint16_t usbmidi_encode(uint8_t cable, const midi_msg *m, uint8_t *out)
{
    uint8_t cn = (uint8_t)(cable << 4);
    uint16_t w = 0;

    if (m->kind == MIDI_KIND_SYSEX) {
        uint8_t i = 0;
        while (i < m->len) {
            uint8_t left = (uint8_t)(m->len - i);
            uint8_t last = (m->flags & MIDI_SYSEX_END) && left <= 3;
            uint8_t take = left < 3 ? left : 3;
            uint8_t cin;
            if (!last) {
                if (left < 3) break;      /* caller must deliver chunks of multiple of 3 except at the end */
                cin = 0x4;                /* SysEx start / continue, 3 bytes */
            } else {
                cin = (uint8_t)(0x4 + take); /* 5, 6, 7: ends with 1, 2, 3 bytes */
            }
            out[w] = (uint8_t)(cn | cin);
            out[w + 1] = m->b[i];
            out[w + 2] = take > 1 ? m->b[i + 1] : 0;
            out[w + 3] = take > 2 ? m->b[i + 2] : 0;
            w += 4;
            i += take;
        }
        return w;
    }
    {
        uint8_t s = m->b[0];
        uint8_t cin;
        if (s >= 0xF8u) cin = 0xF;                    /* single byte */
        else if (s >= 0xF0u) cin = m->len == 1 ? 0x5 : m->len == 2 ? 0x2 : 0x3;
        else cin = (uint8_t)(s >> 4);
        out[0] = (uint8_t)(cn | cin);
        out[1] = s;
        out[2] = m->len > 1 ? m->b[1] : 0;
        out[3] = m->len > 2 ? m->b[2] : 0;
        return 4;
    }
}

void usbmidi_dec_init(usbmidi_dec *d, midi_msg_cb cb, void *ctx)
{
    memset(d, 0, sizeof *d);
    d->cb = cb;
    d->ctx = ctx;
}

static void emit(usbmidi_dec *d, uint8_t kind, uint8_t flags, const uint8_t *b, uint8_t len)
{
    midi_msg m;
    m.kind = kind;
    m.flags = flags;
    m.len = len;
    memcpy(m.b, b, len);
    d->cb(d->ctx, &m);
}

static void sx_flush(usbmidi_dec *d, uint8_t end)
{
    uint8_t flags = (d->sx_first ? MIDI_SYSEX_START : 0) | (end ? MIDI_SYSEX_END : 0);
    emit(d, MIDI_KIND_SYSEX, flags, d->sx, d->sx_len);
    d->sx_first = 0;
    d->sx_len = 0;
}

static void sx_push(usbmidi_dec *d, uint8_t b)
{
    if (d->sx_len == MIDI_SYSEX_CHUNK)
        sx_flush(d, 0);
    d->sx[d->sx_len++] = b;
}

void usbmidi_dec_feed(usbmidi_dec *d, const uint8_t *p, size_t n)
{
    size_t i;
    for (i = 0; i + 4 <= n; i += 4) {
        uint8_t cin = p[i] & 0x0Fu;
        const uint8_t *b = p + i + 1;
        uint8_t k;
        switch (cin) {
        case 0x0: case 0x1:                  /* misc / cable events: ignored */
            break;
        case 0x4:                            /* SysEx start or continue */
            if (!d->in_sysex) { d->in_sysex = 1; d->sx_first = 1; d->sx_len = 0; }
            for (k = 0; k < 3; k++) sx_push(d, b[k]);
            break;
        case 0x5: case 0x6: case 0x7: {      /* SysEx end with 1/2/3 bytes, or 1-byte system common */
            uint8_t cnt = (uint8_t)(cin - 4);
            if (cin == 0x5 && !d->in_sysex && b[0] != 0xF7u) {
                emit(d, b[0] >= 0xF8u ? MIDI_KIND_REALTIME : MIDI_KIND_COMMON, 0, b, 1);
                break;
            }
            if (!d->in_sysex) { d->in_sysex = 1; d->sx_first = 1; d->sx_len = 0; }
            for (k = 0; k < cnt; k++) sx_push(d, b[k]);
            sx_flush(d, 1);
            d->in_sysex = 0;
            break;
        }
        case 0x2: emit(d, MIDI_KIND_COMMON, 0, b, 2); break;
        case 0x3: emit(d, MIDI_KIND_COMMON, 0, b, 3); break;
        case 0xF: emit(d, b[0] >= 0xF8u ? MIDI_KIND_REALTIME : MIDI_KIND_COMMON, 0, b, 1); break;
        case 0xC: case 0xD: emit(d, MIDI_KIND_CHANNEL, 0, b, 2); break;
        default:  emit(d, MIDI_KIND_CHANNEL, 0, b, 3); break;   /* 8 9 A B E */
        }
    }
}
