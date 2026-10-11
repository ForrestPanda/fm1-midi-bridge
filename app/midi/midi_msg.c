/* SPDX-License-Identifier: MIT */
#include "midi_msg.h"
#include <string.h>

uint8_t midi_status_len(uint8_t s)
{
    if (s < 0x80u) return 0;
    if (s < 0xF0u) return ((s & 0xE0u) == 0xC0u) ? 1u : 2u; /* Cn Dn: 1 */
    switch (s) {
    case 0xF1: case 0xF3: return 1;
    case 0xF2: return 2;
    default: return 0;
    }
}

void midi_parser_init(midi_parser *p, midi_msg_cb cb, void *ctx)
{
    memset(p, 0, sizeof *p);
    p->cb = cb;
    p->ctx = ctx;
}

static void emit(midi_parser *p, uint8_t kind, uint8_t flags, const uint8_t *b, uint8_t len)
{
    midi_msg m;
    m.kind = kind;
    m.flags = flags;
    m.len = len;
    memcpy(m.b, b, len);
    p->cb(p->ctx, &m);
}

static void sx_flush(midi_parser *p, uint8_t end)
{
    uint8_t flags = (p->sx_first ? MIDI_SYSEX_START : 0) | (end ? MIDI_SYSEX_END : 0);
    emit(p, MIDI_KIND_SYSEX, flags, p->sx, p->sx_len);
    p->sx_first = 0;
    p->sx_len = 0;
}

static void sx_push(midi_parser *p, uint8_t b)
{
    if (p->sx_len == MIDI_SYSEX_CHUNK)
        sx_flush(p, 0);
    p->sx[p->sx_len++] = b;
}

void midi_parser_feed(midi_parser *p, const uint8_t *data, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        uint8_t b = data[i];
        if (b >= 0xF8u) {                 /* realtime: anywhere, keeps all state */
            if (p->in_sysex && p->sx_len)   /* keep byte order: flush the open chunk */
                sx_flush(p, 0);
            emit(p, MIDI_KIND_REALTIME, 0, &b, 1);
            continue;
        }
        if (b & 0x80u) {                  /* status */
            if (p->in_sysex) {            /* any status ends a SysEx */
                if (b == 0xF7u)
                    sx_push(p, b);
                sx_flush(p, 1);
                p->in_sysex = 0;
                if (b == 0xF7u)
                    continue;
            }
            p->status = 0;
            p->got = 0;
            if (b == 0xF0u) {
                p->in_sysex = 1;
                p->sx_first = 1;
                p->sx_len = 0;
                p->sx[p->sx_len++] = 0xF0u;
                continue;
            }
            if (b == 0xF7u)
                continue;                 /* stray end */
            p->need = midi_status_len(b);
            if (p->need == 0) {           /* F4 F5 F6 */
                emit(p, MIDI_KIND_COMMON, 0, &b, 1);
                continue;
            }
            p->status = b;
            p->running = b < 0xF0u;
            continue;
        }
        /* data byte */
        if (p->in_sysex) {
            sx_push(p, b);
            continue;
        }
        if (p->status == 0)
            continue;                     /* orphan data: dropped */
        if (p->need == 2u && p->got == 0) {
            p->d1 = b;
            p->got = 1;
            continue;
        }
        {
            uint8_t out[3];
            uint8_t len;
            out[0] = p->status;
            if (p->need == 2u) { out[1] = p->d1; out[2] = b; len = 3; }
            else { out[1] = b; len = 2; }
            p->got = 0;
            if (p->running) {
                emit(p, MIDI_KIND_CHANNEL, 0, out, len);
            } else {
                emit(p, MIDI_KIND_COMMON, 0, out, len);
                p->status = 0;            /* system common: no running status */
            }
        }
    }
}
