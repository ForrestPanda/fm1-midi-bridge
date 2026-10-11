/* SPDX-License-Identifier: MIT */
#include "merge.h"
#include <string.h>

void merge_init(midi_merge *g) { memset(g, 0, sizeof *g); }
void merge_all_off(midi_merge *g) { memset(g, 0, sizeof *g); }

int merge_filter(midi_merge *g, const midi_msg *in, midi_msg out[2])
{
    uint8_t s, ch, note;
    if (in->kind != MIDI_KIND_CHANNEL || in->len != 3) { out[0] = *in; return 1; }
    s = in->b[0] & 0xF0; ch = in->b[0] & 0x0F; note = in->b[1] & 0x7F;
    if (s == 0x90 && in->b[2]) {                        /* note on */
        int n = 0;
        if (g->held[ch][note]) {                        /* already sounding: retrigger */
            out[n] = *in; out[n].b[0] = (uint8_t)(0x80 | ch); out[n].b[2] = 0; n++;
        }
        if (g->held[ch][note] < 255) g->held[ch][note]++;
        out[n++] = *in;
        return n;
    }
    if (s == 0x80 || s == 0x90) {                       /* note off */
        if (g->held[ch][note] > 1) { g->held[ch][note]--; return 0; }   /* someone else still holds it */
        g->held[ch][note] = 0;
        out[0] = *in;
        return 1;
    }
    if (s == 0xB0 && (in->b[1] == 120 || in->b[1] == 123)) memset(g->held[ch], 0, 128);
    out[0] = *in;
    return 1;
}
