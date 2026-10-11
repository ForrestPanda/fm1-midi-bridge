/* SPDX-License-Identifier: MIT */
#include "keys.h"
#include <string.h>

void keys_init(keys_state *k, uint8_t channel, int8_t octave, uint8_t velocity)
{
    static const uint8_t cc[KEYS_KNOBS] = { 1, 74, 71, 7 };     /* mod wheel, cutoff, resonance, volume */
    memset(k, 0, sizeof *k);
    k->channel = channel & 0x0F;
    k->octave = octave < -3 ? -3 : octave > 3 ? 3 : octave;
    k->velocity = velocity ? (velocity > 127 ? 127 : velocity) : 100;
    memcpy(k->knob_cc, cc, sizeof cc);
    k->knob_val[3] = 100;
}

uint8_t keys_note_number(const keys_state *k, int key)
{
    int n = KEYS_BASE + key + 12 * k->octave;
    return (uint8_t)(n < 0 ? 0 : n > 127 ? 127 : n);
}

static void fill(midi_msg *m, uint8_t s, uint8_t d1, uint8_t d2)
{
    m->kind = MIDI_KIND_CHANNEL; m->flags = 0; m->len = 3;
    m->b[0] = s; m->b[1] = d1; m->b[2] = d2;
}

int keys_key_event(keys_state *k, int key, int down, midi_msg *out)
{
    if (key < 0 || key >= KEYS_COUNT) return 0;
    if (down) {
        uint8_t note = keys_note_number(k, key);
        if (k->held_note[key]) return 0;                 /* already sounding */
        k->held_note[key] = (uint8_t)(note | 0x80);      /* bit 7: held marker, note 0 is valid */
        fill(out, (uint8_t)(0x90 | k->channel), note, k->velocity);
        return 1;
    }
    if (!k->held_note[key]) return 0;
    fill(out, (uint8_t)(0x80 | k->channel), (uint8_t)(k->held_note[key] & 0x7F), 0);
    k->held_note[key] = 0;
    return 1;
}

int keys_knob_event(keys_state *k, int knob, int delta, midi_msg *out)
{
    int v;
    if (knob < 0 || knob >= KEYS_KNOBS || delta == 0) return 0;
    v = k->knob_val[knob] + delta;
    if (v < 0) v = 0;
    if (v > 127) v = 127;
    if (v == k->knob_val[knob]) return 0;
    k->knob_val[knob] = (uint8_t)v;
    fill(out, (uint8_t)(0xB0 | k->channel), k->knob_cc[knob], (uint8_t)v);
    return 1;
}

int keys_release_all(keys_state *k, midi_msg *out, int max)
{
    int i, n = 0;
    for (i = 0; i < KEYS_COUNT && n < max; i++)
        if (k->held_note[i]) { fill(&out[n++], (uint8_t)(0x80 | k->channel), (uint8_t)(k->held_note[i] & 0x7F), 0); k->held_note[i] = 0; }
    return n;
}

int keys_button_event(const keys_state *k, const btn_map *map, int down, midi_msg *out)
{
    switch (map->type) {
    case BTN_MAP_NOTE:
        fill(out, (uint8_t)((down ? 0x90 : 0x80) | k->channel), (uint8_t)(map->number & 0x7F), down ? k->velocity : 0);
        return 1;
    case BTN_MAP_CC:
        fill(out, (uint8_t)(0xB0 | k->channel), (uint8_t)(map->number & 0x7F), down ? 127 : 0);
        return 1;
    case BTN_MAP_PROGRAM:
        if (!down) return 0;
        out->kind = MIDI_KIND_CHANNEL; out->flags = 0; out->len = 2;
        out->b[0] = (uint8_t)(0xC0 | k->channel); out->b[1] = (uint8_t)(map->number & 0x7F);
        return 1;
    default:
        return 0;
    }
}

int keys_program_step(keys_state *k, int delta, midi_msg *out)
{
    int p = k->program + delta;
    if (p < 0) p = 0;
    if (p > 127) p = 127;
    if (p == k->program && delta) return 0;
    k->program = (uint8_t)p;
    out->kind = MIDI_KIND_CHANNEL; out->flags = 0; out->len = 2;
    out->b[0] = (uint8_t)(0xC0 | k->channel); out->b[1] = k->program;
    return 1;
}

int keys_bank_step(keys_state *k, int delta, midi_msg *out, int max)
{
    int b = k->bank + delta;
    if (max < 2) return 0;
    if (b < 0) b = 0;
    if (b > 127) b = 127;
    if (b == k->bank) return 0;
    k->bank = (uint8_t)b;
    fill(&out[0], (uint8_t)(0xB0 | k->channel), 0, k->bank);
    keys_program_step(k, 0, &out[1]);
    return 2;
}
