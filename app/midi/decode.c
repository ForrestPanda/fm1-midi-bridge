/* SPDX-License-Identifier: MIT */
#include "decode.h"
#include <stdio.h>

static const char *const names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

const char *midi_note_name(uint8_t note, char *buf, int n)
{
    snprintf(buf, (size_t)n, "%s%d", names[note % 12], (int)note / 12 - 1);
    return buf;
}

const char *midi_cc_name(uint8_t cc)
{
    switch (cc) {
    case 0: return "bank msb";   case 1: return "mod";        case 2: return "breath";
    case 4: return "foot";       case 5: return "porta time"; case 7: return "volume";
    case 10: return "pan";       case 11: return "expr";      case 32: return "bank lsb";
    case 64: return "sustain";   case 65: return "porta";     case 66: return "sostenuto";
    case 67: return "soft";      case 71: return "resonance"; case 74: return "cutoff";
    case 120: return "all sound off"; case 121: return "reset ctrls"; case 122: return "local";
    case 123: return "all notes off"; case 124: return "omni off";  case 125: return "omni on";
    case 126: return "mono";     case 127: return "poly";
    default: return "";
    }
}

/* the ids most likely to answer an inquiry on a bench; three-byte ids start with 0x00 */
const char *midi_manufacturer(const uint8_t *id, int *id_len)
{
    static const struct { uint8_t a, b, c; const char *name; } t[] = {
        {0x01,0,0,"Sequential"}, {0x04,0,0,"Moog"}, {0x06,0,0,"Lexicon"}, {0x07,0,0,"Kurzweil"}, {0x0F,0,0,"Ensoniq"},
        {0x10,0,0,"Oberheim"}, {0x18,0,0,"E-mu"}, {0x1C,0,0,"Eventide"}, {0x40,0,0,"Kawai"}, {0x41,0,0,"Roland"},
        {0x42,0,0,"Korg"}, {0x43,0,0,"Yamaha"}, {0x44,0,0,"Casio"}, {0x47,0,0,"Akai"}, {0x7D,0,0,"non-commercial"},
        {0x00,0x00,0x0E,"Alesis"}, {0x00,0x20,0x29,"Novation"}, {0x00,0x20,0x32,"Behringer"}, {0x00,0x20,0x33,"Access"},
        {0x00,0x20,0x3C,"Elektron"}, {0x00,0x20,0x6B,"Arturia"}, {0x00,0x21,0x1D,"Ableton"}, {0x00,0x01,0x05,"M-Audio"},
        {0x00,0x20,0x7E,"Expert Sleepers"}, {0x00,0x21,0x02,"Mutable"}, {0x00,0x01,0x0C,"Native Instruments"},
    };
    unsigned i;
    *id_len = id[0] == 0x00 ? 3 : 1;
    for (i = 0; i < sizeof t / sizeof t[0]; i++) {
        if (*id_len == 1 && t[i].a == id[0] && t[i].a != 0) return t[i].name;
        if (*id_len == 3 && t[i].a == 0 && t[i].b == id[1] && t[i].c == id[2]) return t[i].name;
    }
    return "";
}

int midi_decode(const midi_msg *m, char *buf, int n)
{
    char nb[8];
    uint8_t s = m->b[0];
    int ch = (s & 0x0F) + 1;
    if (m->kind == MIDI_KIND_SYSEX) {
        if ((m->flags & MIDI_SYSEX_START) && m->len >= 5 && m->b[1] == 0x7E && m->b[3] == 0x06 && m->b[4] == 0x01)
            return snprintf(buf, (size_t)n, "SysEx inquiry");
        if ((m->flags & MIDI_SYSEX_START) && m->len >= 5 && m->b[1] == 0x7E && m->b[3] == 0x06 && m->b[4] == 0x02) {
            /* F0 7E dev 06 02 mfr(1|3) family(2) member(2) version(4) F7 */
            int il; const char *name = midi_manufacturer(m->b + 5, &il);
            const uint8_t *p = m->b + 5 + il;
            if (m->len < 5 + il + 8) return snprintf(buf, (size_t)n, "SysEx inquiry reply");
            if (name[0]) return snprintf(buf, (size_t)n, "Inquiry: %s fam %02X%02X v%d.%d.%d.%d", name, p[1], p[0], p[4], p[5], p[6], p[7]);
            if (il == 1) return snprintf(buf, (size_t)n, "Inquiry: id %02X fam %02X%02X v%d.%d", m->b[5], p[1], p[0], p[4], p[5]);
            return snprintf(buf, (size_t)n, "Inquiry: id %02X%02X%02X fam %02X%02X", m->b[5], m->b[6], m->b[7], p[1], p[0]);
        }
        return snprintf(buf, (size_t)n, "SysEx %s%dB", (m->flags & MIDI_SYSEX_START) ? "" : "..", m->len);
    }
    if (m->kind == MIDI_KIND_REALTIME) {
        switch (s) {
        case 0xF8: return snprintf(buf, (size_t)n, "Clock");
        case 0xFA: return snprintf(buf, (size_t)n, "Start");
        case 0xFB: return snprintf(buf, (size_t)n, "Continue");
        case 0xFC: return snprintf(buf, (size_t)n, "Stop");
        case 0xFE: return snprintf(buf, (size_t)n, "Active sensing");
        case 0xFF: return snprintf(buf, (size_t)n, "Reset");
        default:   return snprintf(buf, (size_t)n, "Realtime %02X", s);
        }
    }
    if (m->kind == MIDI_KIND_COMMON) {
        switch (s) {
        case 0xF1: return snprintf(buf, (size_t)n, "MTC %02X", m->b[1]);
        case 0xF2: return snprintf(buf, (size_t)n, "SongPos %d", m->b[1] | m->b[2] << 7);
        case 0xF3: return snprintf(buf, (size_t)n, "SongSel %d", m->b[1]);
        case 0xF6: return snprintf(buf, (size_t)n, "TuneReq");
        default:   return snprintf(buf, (size_t)n, "Common %02X", s);
        }
    }
    switch (s & 0xF0) {
    case 0x80: return snprintf(buf, (size_t)n, "%d NoteOff %s %d", ch, midi_note_name(m->b[1], nb, sizeof nb), m->b[2]);
    case 0x90: return snprintf(buf, (size_t)n, "%d Note%s %s %d", ch, m->b[2] ? "On" : "Off", midi_note_name(m->b[1], nb, sizeof nb), m->b[2]);
    case 0xA0: return snprintf(buf, (size_t)n, "%d PolyAT %s %d", ch, midi_note_name(m->b[1], nb, sizeof nb), m->b[2]);
    case 0xB0: {
        const char *name = midi_cc_name(m->b[1]);
        if (name[0]) return snprintf(buf, (size_t)n, "%d CC%d %s %d", ch, m->b[1], name, m->b[2]);
        return snprintf(buf, (size_t)n, "%d CC%d %d", ch, m->b[1], m->b[2]);
    }
    case 0xC0: return snprintf(buf, (size_t)n, "%d Program %d", ch, m->b[1] + 1);
    case 0xD0: return snprintf(buf, (size_t)n, "%d ChanAT %d", ch, m->b[1]);
    case 0xE0: return snprintf(buf, (size_t)n, "%d Bend %+d", ch, (m->b[1] | m->b[2] << 7) - 8192);
    default:   return snprintf(buf, (size_t)n, "%02X %02X %02X", m->b[0], m->b[1], m->b[2]);
    }
}
