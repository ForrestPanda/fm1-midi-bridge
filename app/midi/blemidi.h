/* SPDX-License-Identifier: MIT */
/* BLE MIDI 1.0 packet encoder and decoder (MIDI over Bluetooth Low Energy, 2015).
 *
 * Encoder: collect messages that arrive within one connection interval into one
 * packet of at most `max` bytes (ATT_MTU - 3). blemidi_enc_add() returns 0 when
 * the message does not fit: send the packet (blemidi_enc_take) and add again.
 * Timestamps are milliseconds (any 32-bit counter; only 13 bits are sent).
 *
 * Decoder: blemidi_dec_feed() parses one received packet and calls back with each
 * message and its 13-bit timestamp. A SysEx may span packets; the decoder keeps
 * state between packets for that.
 */
#pragma once
#include "midi_msg.h"

#define BLEMIDI_SERVICE_UUID "03B80E5A-EDE8-4B33-A751-6CE34EC4C700"
#define BLEMIDI_CHAR_UUID    "7772E5DB-3868-4112-A1A9-F2669D106BF3"
#define BLEMIDI_MAX_PACKET 512u

typedef struct {
    uint8_t buf[BLEMIDI_MAX_PACKET];
    uint16_t len;
    uint16_t max;
    uint8_t last_status;  /* running status inside this packet */
    uint8_t in_sysex;     /* a SysEx is open across packets */
    uint16_t last_ts;     /* 13-bit timestamp of the last message in this packet */
} blemidi_enc;

void blemidi_enc_init(blemidi_enc *e, uint16_t max);
void blemidi_enc_set_max(blemidi_enc *e, uint16_t max);
/* 1 = added, 0 = does not fit (packet unchanged, take it first) */
int blemidi_enc_add(blemidi_enc *e, uint32_t ts_ms, const midi_msg *m);
int blemidi_enc_empty(const blemidi_enc *e);
/* returns the packet bytes and starts a new packet */
const uint8_t *blemidi_enc_take(blemidi_enc *e, uint16_t *len);

typedef void (*blemidi_dec_cb)(void *ctx, uint16_t ts, const midi_msg *m);

typedef struct {
    blemidi_dec_cb cb;
    void *ctx;
    uint8_t in_sysex;
    uint8_t sx_first;
    uint8_t sx_len;
    uint8_t sx[MIDI_SYSEX_CHUNK];
    uint16_t sx_ts;
} blemidi_dec;

void blemidi_dec_init(blemidi_dec *d, blemidi_dec_cb cb, void *ctx);
/* 0 = ok, -1 = malformed (packet dropped, SysEx state reset) */
int blemidi_dec_feed(blemidi_dec *d, const uint8_t *pkt, uint16_t len);
