/* SPDX-License-Identifier: MIT */
/* Record a stretch of MIDI with timing and replay it. Channel, common and realtime
 * messages only (SysEx is skipped). Pure C. */
#pragma once
#include "midi_msg.h"

#define REC_MAX 2048                   /* events; 8 bytes each */

typedef struct { uint32_t t; uint8_t b[3]; uint8_t len_kind; } rec_event;   /* len in bits 0..1, kind in bits 4..5 */

typedef struct midi_recorder {
    rec_event ev[REC_MAX];
    uint16_t count, play_pos;
    uint32_t t0;                        /* ms when recording started */
    uint32_t length;                    /* ms from start to stop: the loop period */
    uint32_t play_t0;                   /* ms when playback started (of this pass) */
    uint8_t recording, playing, loop;
} midi_recorder;

void rec_init(midi_recorder *r);
void rec_clear(midi_recorder *r);
void rec_start(midi_recorder *r, uint32_t now);
void rec_stop(midi_recorder *r, uint32_t now);                            /* the stop time sets the loop length */
void rec_add(midi_recorder *r, const midi_msg *m, uint32_t now);          /* while recording */
uint32_t rec_duration(const midi_recorder *r);                            /* ms, start to stop */
void rec_play(midi_recorder *r, uint32_t now, int loop);
void rec_play_stop(midi_recorder *r);
int  rec_next(midi_recorder *r, uint32_t now, midi_msg *out);             /* 1 = out is due now */
