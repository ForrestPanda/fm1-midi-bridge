/* SPDX-License-Identifier: MIT */
/* MIDI message and byte-stream parser, transport independent.
 *
 * A midi_msg is one complete channel, system common or realtime message
 * (1..3 bytes), or one chunk of a SysEx. SysEx is delivered as chunks flagged
 * MIDI_SYSEX_START / MIDI_SYSEX_END so long dumps never need a buffer; the F0
 * and F7 bytes are part of the first / last chunk.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

#define MIDI_SYSEX_CHUNK 60u   /* a multiple of 3: USB-MIDI packs SysEx 3 bytes per event */

enum {
    MIDI_KIND_CHANNEL = 0,  /* 8n..En */
    MIDI_KIND_COMMON = 1,   /* F1..F6 */
    MIDI_KIND_REALTIME = 2, /* F8..FF */
    MIDI_KIND_SYSEX = 3     /* chunk; see flags */
};
enum { MIDI_SYSEX_START = 1, MIDI_SYSEX_END = 2 };

typedef struct {
    uint8_t kind;
    uint8_t flags; /* sysex only */
    uint8_t len;
    uint8_t b[MIDI_SYSEX_CHUNK];
} midi_msg;

typedef void (*midi_msg_cb)(void *ctx, const midi_msg *m);

typedef struct {
    midi_msg_cb cb;
    void *ctx;
    uint8_t status;   /* status the next data bytes belong to; 0 = none */
    uint8_t running;  /* status is a channel status: keep it after a message */
    uint8_t need;     /* data bytes of status, 1 or 2 */
    uint8_t got;
    uint8_t d1;
    uint8_t in_sysex;
    uint8_t sx_first; /* next SysEx chunk is the first one */
    uint8_t sx_len;
    uint8_t sx[MIDI_SYSEX_CHUNK];
} midi_parser;

void midi_parser_init(midi_parser *p, midi_msg_cb cb, void *ctx);
void midi_parser_feed(midi_parser *p, const uint8_t *data, size_t n);

/* data byte count for a status byte: 0..2; 0 for F0, F4..F7 and realtime */
uint8_t midi_status_len(uint8_t status);
