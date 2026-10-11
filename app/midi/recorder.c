/* SPDX-License-Identifier: MIT */
#include "recorder.h"
#include <string.h>

void rec_init(midi_recorder *r) { memset(r, 0, sizeof *r); }
void rec_clear(midi_recorder *r) { r->count = 0; r->play_pos = 0; r->recording = r->playing = 0; }
void rec_start(midi_recorder *r, uint32_t now) { r->count = 0; r->t0 = now; r->length = 0; r->recording = 1; r->playing = 0; }
void rec_stop(midi_recorder *r, uint32_t now)
{
    if (!r->recording) return;
    r->recording = 0;
    r->length = now - r->t0;
    if (r->count && r->length < r->ev[r->count - 1].t + 1) r->length = r->ev[r->count - 1].t + 1;
}

void rec_add(midi_recorder *r, const midi_msg *m, uint32_t now)
{
    rec_event *e;
    if (!r->recording || r->count >= REC_MAX || m->kind == MIDI_KIND_SYSEX || m->len == 0 || m->len > 3) return;
    e = &r->ev[r->count++];
    e->t = now - r->t0;
    memcpy(e->b, m->b, m->len);
    e->len_kind = (uint8_t)(m->len | (m->kind << 4));
}

uint32_t rec_duration(const midi_recorder *r) { return r->length; }

void rec_play(midi_recorder *r, uint32_t now, int loop)
{
    if (!r->count) return;
    r->recording = 0;
    r->playing = 1;
    r->loop = (uint8_t)loop;
    r->play_pos = 0;
    r->play_t0 = now;
}

void rec_play_stop(midi_recorder *r) { r->playing = 0; }

int rec_next(midi_recorder *r, uint32_t now, midi_msg *out)
{
    const rec_event *e;
    if (!r->playing) return 0;
    if (r->play_pos >= r->count) {
        if (!r->loop) { r->playing = 0; return 0; }
        r->play_pos = 0;
        r->play_t0 += r->length;                       /* keeps the loop in time */
    }
    e = &r->ev[r->play_pos];
    if ((int32_t)(now - (r->play_t0 + e->t)) < 0) return 0;
    out->kind = (uint8_t)((e->len_kind >> 4) & 3);
    out->flags = 0;
    out->len = (uint8_t)(e->len_kind & 3);
    memcpy(out->b, e->b, out->len);
    r->play_pos++;
    return 1;
}
