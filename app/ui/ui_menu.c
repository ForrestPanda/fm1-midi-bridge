/* SPDX-License-Identifier: MIT */
#include "system/includes.h"
#include "ui_menu.h"
#include <stdio.h>

#define BODY_ROWS (UI_ROWS - 2)          /* title row, 15 items, footer row */

static int item_visible(const ui_item *it) { return !it->visible || it->visible(); }
static int item_selectable(const ui_item *it) { return item_visible(it) && it->kind != UI_INFO; }

static int any_selectable(const ui_menu *m)
{
    int i;
    for (i = 0; i < m->n; i++) if (item_selectable(&m->items[i])) return 1;
    return 0;
}

/* the next row the cursor may rest on, in direction dir; from itself when there is none */
static int next_stop(const ui_menu *m, int from, int dir)
{
    int i = from, strict = any_selectable(m);
    for (;;) {
        i += dir;
        if (i < 0 || i >= m->n) return from;
        if (strict ? item_selectable(&m->items[i]) : item_visible(&m->items[i])) return i;
    }
}

static void settle(ui_menu *m)             /* the cursor on a row it may rest on */
{
    int strict = any_selectable(m);
    if (m->cursor >= m->n) m->cursor = m->n ? (u8)(m->n - 1) : 0;
    if (!m->n) return;
    if (strict ? item_selectable(&m->items[m->cursor]) : item_visible(&m->items[m->cursor])) return;
    {
        int down = next_stop(m, m->cursor, 1);
        if (down != m->cursor) { m->cursor = (u8)down; return; }
        m->cursor = (u8)next_stop(m, m->cursor, -1);
    }
}

void ui_menu_enter(ui_menu *m)
{
    if (!ui_came_back()) { m->cursor = 0; m->top = 0; }
    settle(m);
}

void ui_menu_draw(ui_menu *m, const char *title)
{
    char line[UI_COLS + 1], val[UI_COLS + 1];
    int row = 1, i, shown = 0;
    /* keep the cursor inside the window of visible rows */
    if (m->cursor < m->top) m->top = m->cursor;
    for (;;) {
        int vis = 0, k;
        for (k = m->top; k <= m->cursor; k++) if (item_visible(&m->items[k])) vis++;
        if (vis <= BODY_ROWS) break;
        m->top++;
    }
    ui_title(title);
    for (i = m->top; i < m->n && shown < BODY_ROWS; i++) {
        const ui_item *it = &m->items[i];
        int ln, vn, pos = UI_COLS;
        if (!item_visible(it)) continue;
        val[0] = 0;
        if (it->value) it->value(val, sizeof val);
        else if (it->kind == UI_SUBMENU) strcpy(val, ">");
        ln = snprintf(line, sizeof line, " %s", it->label);
        vn = (int)strlen(val);
        if (vn) {
            pos = UI_COLS - 1 - vn;
            if (pos < ln + 1) pos = ln + 1;
            while (ln < pos && ln < UI_COLS) line[ln++] = ' ';
            line[ln] = 0;
            snprintf(line + ln, sizeof line - ln, "%s", val);
        }
        if (i == m->cursor) ui_row_inv((u16)row, line);
        else {
            u16 fg = it->kind == UI_INFO ? UI_C_DIM : UI_C_TXT;
            if (it->warn && it->warn()) ui_row((u16)row, line, UI_C_WARN, UI_C_BG);
            else if (it->kind == UI_TOGGLE && vn) ui_row2((u16)row, line, fg, UI_C_BG, (u16)pos, strncmp(val, "off", 3) == 0 ? UI_C_DIM : UI_C_OK);
            else ui_row((u16)row, line, fg, UI_C_BG);
        }
        row++; shown++;
    }
    while (row < UI_ROWS - 1) ui_row((u16)row++, "", UI_C_TXT, UI_C_BG);
    ui_row(UI_ROWS - 1, m->footer ? m->footer : "SEL:move OCT+:ok OCT-:back", UI_C_DIM, UI_C_BG);
}

void ui_menu_adjust(ui_menu *m, int delta)
{
    const ui_item *it = &m->items[m->cursor];
    if ((it->kind == UI_TOGGLE || it->kind == UI_ENUM) && it->adjust) { it->adjust(delta); ui_invalidate(); }
}

int ui_menu_event(ui_menu *m, const mx_event *e)
{
    const ui_item *it = &m->items[m->cursor];
    if (e->type == MX_EV_ENC && (e->id == MX_ENC_SELECT || e->id == MX_ENC_PRESETS)) {
        m->cursor = (u8)next_stop(m, m->cursor, e->delta > 0 ? 1 : -1);
        ui_invalidate();
        return 1;
    }
    if (e->type == MX_EV_ENC && e->id == MX_ENC_KNOB1) {
        ui_menu_adjust(m, e->delta);
        return 1;
    }
    if (e->type == MX_EV_BTN_HOLD && e->id == MX_BTN_OCT_PLUS) {
        if (it->hold) { it->hold(); ui_invalidate(); }
        return 1;
    }
    if (e->type == MX_EV_BTN_UP && !(e->flags & MX_UP_WAS_HELD)) {
        if (e->id == MX_BTN_OCT_PLUS) {
            if (it->activate) it->activate();                       /* ENUM with activate: OCT+ cycles, KNOB1 adjusts */
            else if ((it->kind == UI_TOGGLE || it->kind == UI_ENUM) && it->adjust) it->adjust(1);
            ui_invalidate();
            return 1;
        }
        if (e->id == MX_BTN_OCT_MINUS) { ui_back(); return 1; }
    }
    return 0;
}
