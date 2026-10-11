/* SPDX-License-Identifier: MIT */
#include "compose.h"

static const char *const kind_names[COMPOSE_KIND_COUNT] = {
    "Note", "CC", "Program", "Pitch bend", "Chan AT", "Poly AT", "Clock", "Start", "Continue", "Stop", "Dev inquiry", "Raw 3 bytes" };
static const char *const port_names[COMPOSE_PORT_COUNT] = { "USB", "BLE", "USB+BLE" };

const char *compose_kind_name(uint8_t k) { return k < COMPOSE_KIND_COUNT ? kind_names[k] : "?"; }
const char *compose_port_name(uint8_t p) { return p < COMPOSE_PORT_COUNT ? port_names[p] : "?"; }
int compose_uses_channel(uint8_t k) { return k <= COMPOSE_POLYAT; }
int compose_uses_d1(uint8_t k) { return k <= COMPOSE_POLYAT || k == COMPOSE_RAW; }
int compose_uses_d2(uint8_t k) { return k == COMPOSE_NOTE || k == COMPOSE_CC || k == COMPOSE_BEND || k == COMPOSE_POLYAT || k == COMPOSE_RAW; }

static void ch3(midi_msg *m, uint8_t s, uint8_t d1, uint8_t d2) { m->kind = MIDI_KIND_CHANNEL; m->flags = 0; m->len = 3; m->b[0] = s; m->b[1] = (uint8_t)(d1 & 0x7F); m->b[2] = (uint8_t)(d2 & 0x7F); }
static void ch2(midi_msg *m, uint8_t s, uint8_t d1) { m->kind = MIDI_KIND_CHANNEL; m->flags = 0; m->len = 2; m->b[0] = s; m->b[1] = (uint8_t)(d1 & 0x7F); }
static void rt(midi_msg *m, uint8_t s) { m->kind = MIDI_KIND_REALTIME; m->flags = 0; m->len = 1; m->b[0] = s; }

int compose_build(const compose_spec *c, midi_msg *out)
{
    uint8_t ch = (uint8_t)(c->channel & 0x0F);
    switch (c->kind) {
    case COMPOSE_NOTE:    ch3(out, (uint8_t)(0x90 | ch), c->d1, c->d2 ? c->d2 : 100); return 1;
    case COMPOSE_CC:      ch3(out, (uint8_t)(0xB0 | ch), c->d1, c->d2); return 1;
    case COMPOSE_PROGRAM: ch2(out, (uint8_t)(0xC0 | ch), c->d1); return 1;
    case COMPOSE_BEND:    ch3(out, (uint8_t)(0xE0 | ch), c->d1, c->d2); return 1;     /* d1 lsb, d2 msb; 0x00 0x40 = centre */
    case COMPOSE_CHANAT:  ch2(out, (uint8_t)(0xD0 | ch), c->d1); return 1;
    case COMPOSE_POLYAT:  ch3(out, (uint8_t)(0xA0 | ch), c->d1, c->d2); return 1;
    case COMPOSE_CLOCK:   rt(out, 0xF8); return 1;
    case COMPOSE_START:   rt(out, 0xFA); return 1;
    case COMPOSE_CONTINUE: rt(out, 0xFB); return 1;
    case COMPOSE_STOP:    rt(out, 0xFC); return 1;
    case COMPOSE_INQUIRY:
        out->kind = MIDI_KIND_SYSEX; out->flags = MIDI_SYSEX_START | MIDI_SYSEX_END; out->len = 6;
        out->b[0] = 0xF0; out->b[1] = 0x7E; out->b[2] = 0x7F; out->b[3] = 0x06; out->b[4] = 0x01; out->b[5] = 0xF7;
        return 1;
    case COMPOSE_RAW: {
        /* channel field holds the status high nibble index 8..E? no: raw = status byte in d1? keep it
         * simple: status = 0x80 | (channel << 4 ... ) is not general. Raw: d1 = status, d2 = data1,
         * channel = data2 (all 0..127 except status, which may be 0x80..0xEF or 0xF1..0xFC). */
        uint8_t s = c->d1, a = c->d2, b = c->channel;
        if (s >= 0xF8) { rt(out, s); return 1; }
        if (s >= 0xF0) { out->kind = MIDI_KIND_COMMON; out->flags = 0; out->len = (uint8_t)(1 + midi_status_len(s)); out->b[0] = s; out->b[1] = (uint8_t)(a & 0x7F); out->b[2] = (uint8_t)(b & 0x7F); return out->len <= 3; }
        if (s < 0x80) return 0;
        if (midi_status_len(s) == 1) ch2(out, s, a); else ch3(out, s, a, b);
        return 1;
    }
    default: return 0;
    }
}

int compose_note_off(const compose_spec *c, midi_msg *out)
{
    if (c->kind != COMPOSE_NOTE) return 0;
    ch3(out, (uint8_t)(0x80 | (c->channel & 0x0F)), c->d1, 0);
    return 1;
}
