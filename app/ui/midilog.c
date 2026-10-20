/* SPDX-License-Identifier: MIT */
#include "system/includes.h"
#include "app_config.h"
#include "midilog.h"
#include "lcd.h"
#include "midi/decode.h"
#include "settings.h"
#include <stdio.h>

#define COLS   (LCD_W / 8)          /* 30 with the 8x14 font */
#define ROWS   (LCD_H / 14)         /* 17 */
#define LINES  (ROWS - 1)
#define RING   MIDILOG_RING

static char ring[RING][COLS + 1];
static u32 head;                    /* lines written so far; line k lives in ring[k % RING] while k >= head - RING */
static u32 view_end;                /* the line after the bottom row while scrolled or paused; 0 = follow head */
static u8 paused;
static char status[COLS + 1], status_shown[COLS + 1];
static volatile u8 dirty;           /* rows and status to redraw */
static volatile u8 dirty_st;        /* only the status line: lines arrived under a held view */
static u8 ready;
static u8 decoded;                  /* 0 = raw bytes, 1 = decoded text */
static u8 raw_ring[RING][2 + MIDI_SYSEX_CHUNK]; /* [port|flags, len, bytes...] so the view can be switched */

static const u16 C_BG = 0x0000, C_TXT = 0xFFFF, C_DIM = 0x8410, C_STAT = 0x07E0;
static const u16 C_TITLE_BG = RGB565(0, 72, 112);           /* the UI's title band (ui.c) */

void midilog_init(void)
{
    memset(ring, 0, sizeof ring);
    head = 0; view_end = 0; paused = 0;
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

static u8 hide_bit(const midi_msg *m)
{
    u8 s = m->b[0] & 0xF0;
    if (m->kind != MIDI_KIND_CHANNEL) return MIDILOG_HIDE_OTHER;
    if (s == 0x80 || s == 0x90) return MIDILOG_HIDE_NOTES;
    if (s == 0xC0) return MIDILOG_HIDE_PC;
    if (s == 0xB0) return MIDILOG_HIDE_CC;
    return MIDILOG_HIDE_OTHER;
}

static void store(int port, int out, const midi_msg *m)
{
    u8 *raw = raw_ring[head % RING];
    if (settings_get()->log_hide & hide_bit(m))
        return;                                         /* a kind the page does not list */
    if (m->kind == MIDI_KIND_SYSEX && !(m->flags & MIDI_SYSEX_START))
        return;                                         /* only the first chunk of a SysEx */
    raw[0] = (u8)(port | (out ? 0x80 : 0) | (m->kind << 4) | ((m->flags & 3) << 6));
    raw[1] = m->len;
    memcpy(raw + 2, m->b, m->len);
    format(ring[head % RING], port, out, m);
    head++;
    if (!view_end) dirty = 1;                           /* a held view stays as it is; its count moves */
    else dirty_st = 1;
}

void midilog_add(int port, const midi_msg *m) { store(port, 0, m); }
void midilog_add_out(int port, const midi_msg *m) { store(port, 1, m); }

void midilog_set_decoded(int on)
{
    u32 k, first = head > RING ? head - RING : 0;
    decoded = (u8)on;
    for (k = first; k < head; k++) {                    /* re-render what the ring holds */
        const u8 *raw = raw_ring[k % RING];
        midi_msg m;
        m.kind = (raw[0] >> 4) & 3; m.flags = (raw[0] >> 6) & 3; m.len = raw[1];
        memcpy(m.b, raw + 2, m.len);
        format(ring[k % RING], raw[0] & 0x0F, raw[0] & 0x80, &m);
    }
    midilog_invalidate();
}

int midilog_decoded(void) { return decoded; }

/* the oldest line index whose window of LINES still lies inside the ring */
static u32 lowest_end(void)
{
    u32 low = head < LINES ? head : LINES;
    if (head > RING && head - RING + LINES > low) low = head - RING + LINES;
    return low;
}

void midilog_scroll(int lines)
{
    u32 end = view_end ? view_end : head, low = lowest_end();
    if (lines < 0) end = end > (u32)(-lines) ? end + lines : 0;
    else end += (u32)lines;
    if (end < low) end = low;
    if (end > head) end = head;
    view_end = (end >= head && !paused) ? 0 : end;
    midilog_invalidate();
}

void midilog_pause(int on)
{
    paused = (u8)(on != 0);
    view_end = paused ? head : 0;
    midilog_invalidate();
}

int midilog_paused(void) { return paused; }

void midilog_invalidate(void)
{
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
    u8 hide = settings_get()->log_hide;
    if (hide) {                                         /* the filter in the routes' place: N P C O, '-' hidden */
        snprintf(s, sizeof s, "USB:%s BLE:%s  show %c%c%c%c",
                 usb_ok ? "ok" : "--", ble_ok ? "ok" : "--",
                 hide & MIDILOG_HIDE_NOTES ? '-' : 'N', hide & MIDILOG_HIDE_PC ? '-' : 'P',
                 hide & MIDILOG_HIDE_CC ? '-' : 'C', hide & MIDILOG_HIDE_OTHER ? '-' : 'O');
        if (strcmp(s, status) != 0) midilog_status(s);
        return;
    }
    snprintf(s, sizeof s, "USB:%s BLE:%s  %s%s%s%s",
             usb_ok ? "ok" : "--", ble_ok ? "ok" : "--",
             router_enabled(r, MIDI_PORT_TRS, MIDI_PORT_BLE) ? "TB " : "",
             router_enabled(r, MIDI_PORT_TRS, MIDI_PORT_USB) ? "TU " : "",
             router_enabled(r, MIDI_PORT_USB, MIDI_PORT_BLE) ? "UB " : "",
             router_enabled(r, MIDI_PORT_BLE, MIDI_PORT_USB) ? "BU" : "");
    if (strcmp(s, status) != 0) midilog_status(s);
}

static void draw_line(u16 row, const char *text, u16 fg, u16 bg)
{
    char buf[COLS + 1];
    int n = snprintf(buf, sizeof buf, "%s", text);
    while (n < COLS) buf[n++] = ' ';               /* pad: overwrites the old line */
    buf[COLS] = 0;
    lcd_text(0, row, buf, fg, bg);
}

void midilog_draw(void)
{
    u32 end, first, i, low;
    char st[COLS + 1];
    if (!ready || (!dirty && !dirty_st)) return;
    end = view_end ? view_end : head;
    low = lowest_end();
    if (end < low) end = low;                      /* the ring moved on under a held view */
    if (end > head) end = head;
    /* the status line, with the view's state on the right (no printf precision: the
     * SDK's printf is minimal) */
    {
        int n = snprintf(st, sizeof st, "%s", status);
        if (paused || (view_end && head > end)) {
            char tail[12];
            int tn;
            if (paused) snprintf(tail, sizeof tail, "PAUSED");
            else snprintf(tail, sizeof tail, "^%u", (unsigned)(head - end));
            tn = (int)strlen(tail);
            if (n > COLS - tn - 1) n = COLS - tn - 1;
            while (n < COLS - tn) st[n++] = ' ';
            memcpy(st + n, tail, (size_t)tn + 1);
        }
    }
    if (strcmp(st, status_shown) != 0) {
        draw_line(0, st, (paused || view_end) ? C_TXT : C_STAT, C_TITLE_BG);
        strcpy(status_shown, st);
    }
    dirty_st = 0;
    if (!dirty) return;
    dirty = 0;
    first = end > LINES ? end - LINES : 0;
    for (i = 0; i < LINES; i++) {                  /* newest at the bottom; all rows (no hardware scroll) */
        u32 k = first + i;
        draw_line((u16)(1 + i), k < end ? ring[k % RING] : "", k + 1 == end ? C_TXT : C_DIM, C_BG);
    }
}
