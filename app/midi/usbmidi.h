/* SPDX-License-Identifier: MIT */
/* USB-MIDI 1.0 event packets (4 bytes: cable/CIN, then up to 3 MIDI bytes). */
#pragma once
#include "midi_msg.h"

/* Encode a message as one or more 4-byte packets into out (room for
 * (m->len / 3 + 1) * 4 bytes). Returns the byte count written. */
uint16_t usbmidi_encode(uint8_t cable, const midi_msg *m, uint8_t *out);

typedef struct {
    midi_msg_cb cb;
    void *ctx;
    uint8_t in_sysex;
    uint8_t sx_first;
    uint8_t sx_len;
    uint8_t sx[MIDI_SYSEX_CHUNK];
} usbmidi_dec;

void usbmidi_dec_init(usbmidi_dec *d, midi_msg_cb cb, void *ctx);
/* feed whole 4-byte packets (n a multiple of 4); cable number is ignored */
void usbmidi_dec_feed(usbmidi_dec *d, const uint8_t *pkts, size_t n);
