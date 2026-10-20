/* SPDX-License-Identifier: MIT */
/* The UI task: owns the panel, reads the matrix events, runs the pages.
 * Navigation: SELECT (or PRESETS) encoder moves, OCT+ = OK / toggle, hold OCT+ = the
 * row's second action where it has one, OCT- = back, HOME = the status page from
 * anywhere (the menu from the status page), hold HOME = the Perform page, KNOB1
 * changes the highlighted value (fast turns step by 3 and by 10). */
#pragma once
#include "typedef.h"
#include "input/matrix.h"
#include "midi/keys.h"

enum { PAGE_SPLASH, PAGE_MAIN, PAGE_ROUTES, PAGE_LOG, PAGE_BLE, PAGE_BLE_SCAN, PAGE_USB,
       PAGE_SYSTEM, PAGE_DISPLAY, PAGE_FILTERS, PAGE_CONFIRM, PAGE_INPUT, PAGE_KEYBOARD, PAGE_ROUTE_OPT, PAGE_INPUTS, PAGE_BUTTONS, PAGE_PRESETS, PAGE_COMPOSER, PAGE_CONNECTIONS, PAGE_STATS, PAGE_RECORDER, PAGE_REFERENCE, PAGE_CLOCK,
       PAGE_PRESET_OPT, PAGE_PERFORM, PAGE_COUNT };
#define PAGE_HOME PAGE_CONNECTIONS          /* the status page: after the splash, HOME, and the root of OCT- */

typedef struct {
    const char *title;
    void (*enter)(void);
    void (*draw)(void);                     /* full draw after enter / invalidate */
    void (*on_event)(const mx_event *e);
    void (*tick)(void);                     /* every 100 ms */
} ui_page;

void ui_init(void);                         /* creates the task */
void ui_goto(int page);
void ui_back(void);
void ui_home(void);                         /* the status page, stack emptied */
int  ui_current(void);
int  ui_came_back(void);                    /* inside enter(): 1 when reached by OCT- / a closed dialog (keep the cursor) */
void ui_invalidate(void);                   /* force a full redraw of the current page */
void ui_refresh(void);                      /* redraw the current page through the row cache (values moved) */
void ui_confirm(const char *text, void (*yes)(void));
void ui_toast(const char *text);            /* a short note on the footer row for about a second */

/* drawing helpers with a row cache: only changed rows reach the panel */
#define UI_COLS 30
#define UI_ROWS 17
void ui_row(u16 row, const char *text, u16 fg, u16 bg);
void ui_row2(u16 row, const char *text, u16 fg, u16 bg, u16 split, u16 fg2);   /* columns from split on in fg2 */
void ui_row_inv(u16 row, const char *text);               /* inverted (title, cursor) */
void ui_title(const char *title);                         /* row 0: the title band with the link status on the right */
void ui_clear(void);
void ui_status_short(char *buf, int n);                   /* "U:ok B:adv" */

/* battery: read once a second from the divider on PB1 (uncalibrated: verify on hardware) */
u16  ui_battery_mv(void);          /* at the pin (the cell is about twice that: a 1:2 divider, by its readings) */
u16  ui_battery_raw(void);         /* PB1, 10-bit, smoothed */
void ui_battery_bar(char *buf, int n);                    /* "[##..]" */
int  ui_battery_low(void);

keys_state *ui_keys(void);
void ui_keys_reload(void);
void ui_settings_changed(void);                           /* from another task: the UI applies invert, keys, redraw at its next tick */
void ui_octave_shift(int delta);                          /* the keyboard's octave, -3..3 */

/* colours: the title row is a dark blue band with white text (the link status in a
 * lighter tone), the cursor row a white bar with black text, so the two never look alike */
extern const u16 UI_C_BG, UI_C_TXT, UI_C_DIM, UI_C_OK, UI_C_WARN, UI_C_TITLE_BG, UI_C_TITLE_ST;
