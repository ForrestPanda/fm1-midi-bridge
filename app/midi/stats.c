/* SPDX-License-Identifier: MIT */
#include "stats.h"
#include <string.h>

void stats_reset(midi_stats *s)
{
    memset(s, 0, sizeof *s);
    s->note_min = 255; s->vel_min = 255;
}

const char *stats_kind_name(int k)
{
    static const char *const n[ST_KIND_COUNT] = { "notes", "CC", "program", "bend", "aftertouch", "clock", "transport", "SysEx", "other" };
    return k >= 0 && k < ST_KIND_COUNT ? n[k] : "?";
}

void stats_add(midi_stats *s, const midi_msg *m, uint32_t now)
{
    s->last_ms = now;
    if (m->kind == MIDI_KIND_SYSEX) { if (m->flags & MIDI_SYSEX_START) s->kind[ST_SYSEX]++; return; }
    if (m->kind == MIDI_KIND_REALTIME) {
        if (m->b[0] == 0xF8) {
            s->kind[ST_CLOCK]++;
            if (s->clock_count == 0) s->clock_t0 = now;
            s->clock_count++;
            if (s->clock_count > 96) {                     /* 4 quarters: tempo = 60000 * 96 / (24 * dt) */
                uint32_t dt = now - s->clock_t0;
                if (dt > 0) s->bpm_x10 = (uint16_t)((60000u * 10u * (s->clock_count - 1) / 24u + dt / 2) / dt);
                s->clock_count = 0;
            }
        } else if (m->b[0] >= 0xFA && m->b[0] <= 0xFC) s->kind[ST_TRANSPORT]++;
        else s->kind[ST_OTHER]++;
        return;
    }
    if (m->kind != MIDI_KIND_CHANNEL) { s->kind[ST_OTHER]++; return; }
    s->channel[m->b[0] & 0x0F]++;
    switch (m->b[0] & 0xF0) {
    case 0x90:
        if (m->b[2]) {
            s->kind[ST_NOTE]++;
            if (m->b[1] < s->note_min) s->note_min = m->b[1];
            if (m->b[1] > s->note_max) s->note_max = m->b[1];
            if (m->b[2] < s->vel_min) s->vel_min = m->b[2];
            if (m->b[2] > s->vel_max) s->vel_max = m->b[2];
        }
        break;
    case 0x80: break;
    case 0xB0: s->kind[ST_CC]++; break;
    case 0xC0: s->kind[ST_PROGRAM]++; break;
    case 0xE0: s->kind[ST_BEND]++; break;
    case 0xA0: case 0xD0: s->kind[ST_AFTERTOUCH]++; break;
    default: s->kind[ST_OTHER]++; break;
    }
}
