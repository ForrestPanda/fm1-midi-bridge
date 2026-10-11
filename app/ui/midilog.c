/* SPDX-License-Identifier: MIT */
#include "system/includes.h"
#include "app_config.h"
#include "midilog.h"
#include "lcd.h"
#include "midi/decode.h"
#include <stdio.h>

#define COLS   (LCD_W / 8)          /* 30 with the 8x14 font */
#define ROWS   (LCD_H / 14)         /* 17 */
#define LINES  (ROWS - 1)

static char ring[LINES][COLS + 1];
static u32 head;                    /* lines written so far */
static u32 shown;                   /* lines drawn so far */
static char status[COLS + 1], status_shown[COLS + 1];
static volatile u8 dirty;
static u8 ready;
static u8 decoded;                  /* 0 = raw bytes, 1 = decoded text */
static u8 raw_ring[LINES][2 + MIDI_SYSEX_CHUNK]; /* [port|flags, len, bytes...] so the view can be switched */

static const u16 C_BG = 0x0000, C_TXT = 0xFFFF, C_DIM = 0x8410, C_STAT = 0x07E0;

void midilog_init(void)
{
    memset(ring, 0, sizeof ring);
    head = shown = 0;
    strcpy(status, "FM-1 MIDI bridge");
    status_shown[0] = 0;
    ready = 1;                                /* the UI task initialises the panel */
    dirty = 1;
}

static const char *port_name(int port)
{
    return router_port_name(port);
}

static void format(char *line, int port, int out, const midi_msg *m)
{
    int n, i;
    n = snprintf(line, COLS + 1, "%s%s ", out ? ">" : "", port_name(port));
    if (decoded) {
        midi_decode(m, line + n, COLS + 1 - n);
        return;
    }
    if (m->kind == MIDI_KIND_SYSEX) {
        n += snprintf(line + n, COLS + 1 - n, "F0");
        for (i = 1; i < m->len && n < COLS - 3 && m->b[i] != 0xF7; i++)
            n += snprintf(line + n, COLS + 1 - n, " %02X", m->b[i]);
        snprintf(line + n, COLS + 1 - n, (m->flags & MIDI_SYSEX_END) ? " F7" : " ..");
    } else {
        for (i = 0; i < m->len && n < COLS - 3; i++)
            n += snprintf(line + n, COLS + 1 - n, i ? " %02X" : "%02X", m->b[i]);
    }
}

static void store(int port, int out, const midi_msg *m)
{
    u8 *raw = raw_ring[head % LINES];
    if (m->kind == MIDI_KIND_SYSEX && !(m->flags & MIDI_SYSEX_START))
        return;                                         /* only the first chunk of a SysEx */
    raw[0] = (u8)(port | (out ? 0x80 : 0) | (m->kind << 4) | ((m->flags & 3) << 6));
    raw[1] = m->len;
    memcpy(raw + 2, m->b, m->len);
    format(ring[head % LINES], port, out, m);
    head++;
    dirty = 1;
}

void midilog_add(int port, const midi_msg *m) { store(port, 0, m); }
void midilog_add_out(int port, const midi_msg *m) { store(port, 1, m); }

void midilog_set_decoded(int on)
{
    u32 k, first = head > LINES ? head - LINES : 0;
    decoded = (u8)on;
    for (k = first; k < head; k++) {                    /* re-render what is on screen */
        const u8 *raw = raw_ring[k % LINES];
        midi_msg m;
        m.kind = (raw[0] >> 4) & 3; m.flags = (raw[0] >> 6) & 3; m.len = raw[1];
        memcpy(m.b, raw + 2, m.len);
        format(ring[k % LINES], raw[0] & 0x0F, raw[0] & 0x80, &m);
    }
    midilog_invalidate();
}

int midilog_decoded(void) { return decoded; }

void midilog_invalidate(void)
{
    shown = 0;
    status_shown[0] = 0;
    dirty = 1;
}

void midilog_status(const char *s)
{
    snprintf(status, sizeof status, "%s", s);
    dirty = 1;
}

void midilog_status_from(const midi_router *r, int usb_ok, int ble_ok)
{
    char s[COLS + 1];
    snprintf(s, sizeof s, "USB:%s BLE:%s  %s%s%s%s",
             usb_ok ? "ok" : "--", ble_ok ? "ok" : "--",
             router_enabled(r, MIDI_PORT_TRS, MIDI_PORT_BLE) ? "TB " : "",
             router_enabled(r, MIDI_PORT_TRS, MIDI_PORT_USB) ? "TU " : "",
             router_enabled(r, MIDI_PORT_USB, MIDI_PORT_BLE) ? "UB " : "",
             router_enabled(r, MIDI_PORT_BLE, MIDI_PORT_USB) ? "BU" : "");
    if (strcmp(s, status) != 0) midilog_status(s);
}

static void draw_line(u16 row, const char *text, u16 fg)
{
    char buf[COLS + 1];
    int n = snprintf(buf, sizeof buf, "%s", text);
    while (n < COLS) buf[n++] = ' ';               /* pad: overwrites the old line */
    buf[COLS] = 0;
    lcd_text(0, row, buf, fg, C_BG);
}

void midilog_draw(void)
{
    u32 first, i;
    if (!ready || !dirty) return;
    dirty = 0;
    if (strcmp(status, status_shown) != 0) {
        draw_line(0, status, C_STAT);
        strcpy(status_shown, status);
    }
    if (head == shown) return;
    if (head - shown >= LINES || head <= LINES) {
        /* redraw everything: newest at the bottom */
        first = head > LINES ? head - LINES : 0;
        for (i = 0; i < LINES; i++) {
            u32 k = first + i;
            draw_line((u16)(1 + i), k < head ? ring[k % LINES] : "", k + 1 == head ? C_TXT : C_DIM);
        }
    } else {
        /* scrolled by a few lines: redraw all rows (no hardware scroll with a
         * top-left origin MADCTL); cheap enough at 16 rows of 30 cells */
        first = head - LINES;
        for (i = 0; i < LINES; i++) {
            u32 k = first + i;
            draw_line((u16)(1 + i), ring[k % LINES], k + 1 == head ? C_TXT : C_DIM);
        }
    }
    shown = head;
}
