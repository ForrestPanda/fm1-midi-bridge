/* SPDX-License-Identifier: MIT */
/* The composer: a message described by kind, channel and two data bytes, built into
 * a midi_msg. Pure C, used by the panel page and stored as favourites. */
#pragma once
#include "midi_msg.h"

enum { COMPOSE_NOTE = 0, COMPOSE_CC, COMPOSE_PROGRAM, COMPOSE_BEND, COMPOSE_CHANAT, COMPOSE_POLYAT,
       COMPOSE_CLOCK, COMPOSE_START, COMPOSE_CONTINUE, COMPOSE_STOP, COMPOSE_INQUIRY, COMPOSE_RAW, COMPOSE_KIND_COUNT };
enum { COMPOSE_TO_USB = 0, COMPOSE_TO_BLE, COMPOSE_TO_BOTH, COMPOSE_PORT_COUNT };

typedef struct { uint8_t kind, channel, d1, d2, port; } compose_spec;   /* channel 0..15 */

const char *compose_kind_name(uint8_t kind);
const char *compose_port_name(uint8_t port);
int compose_uses_channel(uint8_t kind);
int compose_uses_d1(uint8_t kind);
int compose_uses_d2(uint8_t kind);
/* builds the message; for COMPOSE_NOTE a note on (and compose_note_off builds the off) */
int compose_build(const compose_spec *c, midi_msg *out);
int compose_note_off(const compose_spec *c, midi_msg *out);
