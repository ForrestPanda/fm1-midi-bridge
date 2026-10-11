/* SPDX-License-Identifier: MIT */
/* Traffic statistics: what a device is actually sending. Fed from the router tap. */
#pragma once
#include "midi_msg.h"

enum { ST_NOTE = 0, ST_CC, ST_PROGRAM, ST_BEND, ST_AFTERTOUCH, ST_CLOCK, ST_TRANSPORT, ST_SYSEX, ST_OTHER, ST_KIND_COUNT };

typedef struct midi_stats {
    uint32_t kind[ST_KIND_COUNT];
    uint32_t channel[16];        /* channel messages per channel */
    uint8_t  note_min, note_max; /* 255/0 = none */
    uint8_t  vel_min, vel_max;
    uint32_t clock_count;        /* F8 seen since the last tempo estimate window */
    uint32_t clock_t0;           /* ms of the first clock in the window */
    uint16_t bpm_x10;            /* 0 = unknown; estimated from 24 clocks per quarter */
    uint32_t last_ms;
} midi_stats;

void stats_reset(midi_stats *s);
void stats_add(midi_stats *s, const midi_msg *m, uint32_t now_ms);
const char *stats_kind_name(int kind);
