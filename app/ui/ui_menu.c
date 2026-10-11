/* SPDX-License-Identifier: MIT */
#include "system/includes.h"
#include "ui_menu.h"
#include <stdio.h>

#define BODY_ROWS (UI_ROWS - 2)          /* title row, 15 items, footer row */

static int item_visible(const ui_item *it) { return !it->visible || it->visible(); }

static int next_visible(ui_menu *m, int from, int dir)
{
    int i = from;
    for (;;) {
        i += dir;
        if (i < 0 || i >= m->n) return from;
        if (item_visible(&m->items[i])) return i;
    }
}

void ui_menu_reset(ui_menu *m)
{
    m->cursor = 0; m->top = 0;
    if (m->n && !item_visible(&m->items[0])) m->cursor = (u8)next_visible(m, 0, 1);
}

void ui_menu_draw(ui_menu *m, const char *title)
{
    char line[UI_COLS + 1], val[UI_COLS + 1], tbuf[UI_COLS + 1];
    int row = 1, i, shown = 0;
    /* keep the cursor inside the window of visible rows */
    if (m->cursor < m->top) m->top = m->cursor;
    for (;;) {
        int vis = 0, k;
        for (k = m->top; k <= m->cursor; k++) if (item_visible(&m->items[k])) vis++;
        if (vis <= BODY_ROWS) break;
        m->top++;
    }
    snprintf(tbuf, sizeof tbuf, " %s", title);
    ui_row_inv(0, tbuf);
    for (i = m->top; i < m->n && shown < BODY_ROWS; i++) {
        const ui_item *it = &m->items[i];
        int ln, vn;
        if (!item_visible(it)) continue;
        val[0] = 0;
        if (it->value) it->value(val, sizeof val);
        else if (it->kind == UI_SUBMENU) strcpy(val, ">");
        ln = snprintf(line, sizeof line, " %s", it->label);
        vn = (int)strlen(val);
        if (vn) {
            int pos = UI_COLS - 1 - vn;
            if (pos < ln + 1) pos = ln + 1;
            while (ln < pos && ln < UI_COLS) line[ln++] = ' ';
            line[ln] = 0;
            snprintf(line + ln, sizeof line - ln, "%s", val);
        }
        if (i == m->cursor) ui_row_inv((u16)row, line);
        else ui_row((u16)row, line, it->kind == UI_INFO ? UI_C_DIM : UI_C_TXT, UI_C_BG);
        row++; shown++;
    }
    while (row < UI_ROWS - 1) ui_row((u16)row++, "", UI_C_TXT, UI_C_BG);
    ui_row(UI_ROWS - 1, "SEL:move OCT+:ok OCT-:back", UI_C_DIM, UI_C_BG);
}

int ui_menu_event(ui_menu *m, const mx_event *e)
{
    const ui_item *it = &m->items[m->cursor];
    if (e->type == MX_EV_ENC && (e->id == MX_ENC_SELECT || e->id == MX_ENC_PRESETS)) {
        m->cursor = (u8)next_visible(m, m->cursor, e->delta > 0 ? 1 : -1);
        ui_invalidate();
        return 1;
    }
    if (e->type == MX_EV_ENC && e->id == MX_ENC_KNOB1) {
        if ((it->kind == UI_TOGGLE || it->kind == UI_ENUM) && it->adjust) { it->adjust(e->delta); ui_invalidate(); }
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
