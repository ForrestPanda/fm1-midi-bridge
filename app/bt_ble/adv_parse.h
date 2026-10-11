/* SPDX-License-Identifier: MIT */
/* BLE advertising data (AD structures) parser: does this report advertise the MIDI
 * service, and what name does it carry? Pure C, host-tested. */
#pragma once
#include <stdint.h>

extern const uint8_t midi_service_uuid_le[16];     /* 03B80E5A-EDE8-4B33-A751-6CE34EC4C700 */

/* 1 if a 0x06 / 0x07 (incomplete / complete 128-bit service list) contains the MIDI UUID */
int adv_has_midi_service(const uint8_t *ad, uint8_t len);
/* copies the local name (0x09 complete, else 0x08 shortened) into name[n]; returns its length, 0 if none */
int adv_get_name(const uint8_t *ad, uint8_t len, char *name, int n);
