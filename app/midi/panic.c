/* SPDX-License-Identifier: MIT */
#include "panic.h"

void midi_panic_start(midi_panic *p) { p->i = 0; p->end = MIDI_PANIC_COUNT; }
void midi_panic_start_short(midi_panic *p) { p->i = 0; p->end = MIDI_PANIC_SHORT_COUNT; }

int midi_panic_active(const midi_panic *p) { return p->i < p->end; }

int midi_panic_next(midi_panic *p, midi_msg *out)
{
    uint16_t i = p->i;
    if (i >= p->end) return 0;
    out->kind = MIDI_KIND_CHANNEL;
    out->flags = 0;
    out->len = 3;
    if (i < 32) {
        out->b[0] = (uint8_t)(0xB0 | (i >> 1));
        out->b[1] = (i & 1) ? 123 : 120;
        out->b[2] = 0;
    } else {
        uint16_t k = (uint16_t)(i - 32);
        out->b[0] = (uint8_t)(0x80 | (k >> 7));
        out->b[1] = (uint8_t)(k & 0x7F);
        out->b[2] = 0;
    }
    p->i = (uint16_t)(i + 1);
    return 1;
}
