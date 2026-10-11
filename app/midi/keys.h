/* SPDX-License-Identifier: MIT */
/* The FM-1's own keyboard as a MIDI source: 27 keys (F3..G5) become note on/off,
 * the four knobs become control changes with an absolute value each. Pure C. */
#pragma once
#include "midi_msg.h"

#define KEYS_COUNT   27
#define KEYS_BASE    53          /* MIDI note of key 0 = F3 (C4 = 60) */
#define KEYS_KNOBS   4

enum { BTN_MAP_NONE = 0, BTN_MAP_NOTE, BTN_MAP_CC, BTN_MAP_PROGRAM, BTN_MAP_COUNT };
typedef struct { uint8_t type; uint8_t number; } btn_map;   /* a function button's MIDI job */

typedef struct {
    uint8_t channel;             /* 0..15 */
    uint8_t program;             /* 0..127, last program change sent */
    uint8_t bank;                /* 0..127, bank select MSB (CC 0) */
    int8_t  octave;              /* -3..+3 */
    uint8_t velocity;            /* 1..127 */
    uint8_t knob_cc[KEYS_KNOBS];
    uint8_t knob_val[KEYS_KNOBS];
    uint8_t held_note[KEYS_COUNT];   /* note sounding for each key, 0 = none (so octave changes release correctly) */
} keys_state;

void    keys_init(keys_state *k, uint8_t channel, int8_t octave, uint8_t velocity);
uint8_t keys_note_number(const keys_state *k, int key);
int     keys_key_event(keys_state *k, int key, int down, midi_msg *out);        /* 1 = out filled */
int     keys_knob_event(keys_state *k, int knob, int delta, midi_msg *out);     /* 1 = out filled */
int     keys_release_all(keys_state *k, midi_msg *out, int max);                /* note offs for held keys */
/* a mapped function button: note on/off, CC 127/0, or a program change on press */
int     keys_button_event(const keys_state *k, const btn_map *map, int down, midi_msg *out);
int     keys_program_step(keys_state *k, int delta, midi_msg *out);             /* program change */
int     keys_bank_step(keys_state *k, int delta, midi_msg *out, int max);       /* CC0 bank MSB + program change */
