/* SPDX-License-Identifier: MIT */
/* MIDI panic: a resumable generator of "everything off" messages so the bridge can
 * emit them a few at a time without blocking live traffic.
 *   CC 120 (all sound off) and CC 123 (all notes off) on channels 1..16,
 *   then note off, velocity 0, for notes 0..127 on channels 1..16.
 * 32 + 2048 = 2080 messages in all. */
#pragma once
#include "midi_msg.h"

#define MIDI_PANIC_COUNT (32u + 16u * 128u)

#define MIDI_PANIC_SHORT_COUNT 32u    /* the CC 120 / CC 123 part only */

typedef struct { uint16_t i; uint16_t end; } midi_panic;

void midi_panic_start(midi_panic *p);          /* everything: 2080 messages */
void midi_panic_start_short(midi_panic *p);    /* all sound off + all notes off on 16 channels: 32 messages */
int  midi_panic_next(midi_panic *p, midi_msg *out);   /* 1 = out filled, 0 = done */
int  midi_panic_active(const midi_panic *p);
