/* SPDX-License-Identifier: MIT */
/* A generic list page: label on the left, value on the right, cursor row inverted. */
#pragma once
#include "ui.h"

enum { UI_ACTION, UI_TOGGLE, UI_ENUM, UI_INFO, UI_SUBMENU };

typedef struct {
    const char *label;
    u8 kind;
    void (*value)(char *buf, int n);        /* right column text; NULL = none */
    void (*adjust)(int delta);              /* TOGGLE / ENUM: KNOB1 or OCT+ */
    void (*activate)(void);                 /* ACTION / SUBMENU: OCT+ */
    int (*visible)(void);                   /* NULL = always */
} ui_item;

typedef struct {
    const ui_item *items;
    u8 n, cursor, top;
} ui_menu;

void ui_menu_draw(ui_menu *m, const char *title);
int  ui_menu_event(ui_menu *m, const mx_event *e);   /* 1 = consumed */
void ui_menu_reset(ui_menu *m);
