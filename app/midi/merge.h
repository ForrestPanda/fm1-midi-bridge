/* SPDX-License-Identifier: MIT */
/* Note-collision guard for an output fed by several sources: a note that is still
 * held by one source is not switched off by another, and a note on for a note that is
 * already sounding is preceded by a note off so it retriggers cleanly. Keeps a hold
 * count per (channel, note). Pure C. */
#pragma once
#include "midi_msg.h"

typedef struct { uint8_t held[16][128]; } midi_merge;

void merge_init(midi_merge *g);
/* filters/rewrites a message for one output: returns the number of messages to send (0..2) in out[] */
int  merge_filter(midi_merge *g, const midi_msg *in, midi_msg out[2]);
void merge_all_off(midi_merge *g);          /* forget everything (after a panic) */
