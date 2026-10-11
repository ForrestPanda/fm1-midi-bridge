/* SPDX-License-Identifier: MIT */
/* Human-readable MIDI: "1 NoteOn C4 100", "1 CC7 volume 127", "Clock", "SysEx 6B". */
#pragma once
#include "midi_msg.h"

/* writes at most n-1 characters; returns the length it wanted */
int midi_decode(const midi_msg *m, char *buf, int n);
const char *midi_note_name(uint8_t note, char *buf, int n);     /* "C4", "F#-1" */
const char *midi_cc_name(uint8_t cc);                           /* "volume" or "" */
/* manufacturer from a SysEx id: one byte, or 0x00 followed by two; "" if unknown */
const char *midi_manufacturer(const uint8_t *id, int *id_len);
