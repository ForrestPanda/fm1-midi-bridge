/* SPDX-License-Identifier: MIT */
/* A generic list page: label on the left, value on the right, cursor row inverted.
 * The cursor only stops on rows that do something (INFO rows and blank separators
 * are skipped); a TOGGLE's value is green when on and dim when off; a row with a
 * warn() that returns true is drawn in the warn colour. */
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
    void (*hold)(void);                     /* hold OCT+: the row's second action; NULL = none */
    int (*warn)(void);                      /* NULL = never; 1 = draw the row in the warn colour */
} ui_item;

typedef struct {
    const ui_item *items;
    u8 n, cursor, top;
    const char *footer;                     /* NULL = "SEL:move OCT+:ok OCT-:back" */
} ui_menu;

void ui_menu_draw(ui_menu *m, const char *title);
int  ui_menu_event(ui_menu *m, const mx_event *e);   /* 1 = consumed */
void ui_menu_enter(ui_menu *m);                      /* page enter: cursor to the top, or kept when coming back */
void ui_menu_adjust(ui_menu *m, int delta);          /* adjust the highlighted row (pages with their own keys) */
