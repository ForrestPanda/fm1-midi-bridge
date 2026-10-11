/* SPDX-License-Identifier: MIT */
/* The UI task: owns the panel, reads the matrix events, runs the pages.
 * Navigation: SELECT (or PRESETS) encoder moves, OCT+ = OK / toggle, OCT- = back,
 * HOME = main menu from anywhere, KNOB1 changes the highlighted value. */
#pragma once
#include "typedef.h"
#include "input/matrix.h"
#include "midi/keys.h"

enum { PAGE_SPLASH, PAGE_MAIN, PAGE_ROUTES, PAGE_LOG, PAGE_BLE, PAGE_BLE_SCAN, PAGE_USB,
       PAGE_SYSTEM, PAGE_DISPLAY, PAGE_FILTERS, PAGE_CONFIRM, PAGE_INPUT, PAGE_KEYBOARD, PAGE_ROUTE_OPT, PAGE_INPUTS, PAGE_BUTTONS, PAGE_PRESETS, PAGE_COMPOSER, PAGE_CONNECTIONS, PAGE_STATS, PAGE_RECORDER, PAGE_REFERENCE, PAGE_CLOCK, PAGE_COUNT };

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
void ui_home(void);
int  ui_current(void);
void ui_invalidate(void);                   /* force a full redraw of the current page */
void ui_confirm(const char *text, void (*yes)(void));

/* drawing helpers with a row cache: only changed rows reach the panel */
#define UI_COLS 30
#define UI_ROWS 17
void ui_row(u16 row, const char *text, u16 fg, u16 bg);
void ui_row_inv(u16 row, const char *text);               /* inverted (title, cursor) */
void ui_clear(void);

keys_state *ui_keys(void);
void ui_keys_reload(void);

extern const u16 UI_C_BG, UI_C_TXT, UI_C_DIM, UI_C_OK, UI_C_WARN;
