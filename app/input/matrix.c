/* SPDX-License-Identifier: MIT */
#include "matrix.h"
#include <string.h>

/* on the target the per-tick path runs from an ISR: keep it in internal RAM and keep
 * its tables in RAM too (flash may be switched off during a write) */
#ifdef CONFIG_CPU_WL82
#include "system/generic/typedef.h"
#define MX_RAM_CODE AT(.volatile_ram_code)
#else
#define MX_RAM_CODE
#endif

/* positions: (row, col); the note keys occupy the remaining cells of rows PA6..PA8
 * in column order (verify on hardware) */
const mx_pos mx_btn_pos[MX_BTN_COUNT] = {
    {MX_ROW_PA8, 0}, {MX_ROW_PA8, 1},                                   /* OCT-, OCT+ */
    {MX_ROW_PA5, 6}, {MX_ROW_PA5, 4}, {MX_ROW_PA5, 2}, {MX_ROW_PA5, 0}, /* FX SCL ENV LFO */
    {MX_ROW_PA6, 9}, {MX_ROW_PA6, 8}, {MX_ROW_PA5, 7}, {MX_ROW_PA5, 5}, /* EDIT GLO HOME SAVE */
    {MX_ROW_PA5, 3}, {MX_ROW_PA5, 1}, {MX_ROW_PA6, 10}, {MX_ROW_PA6, 7},/* ARP SEQ PLAY REC */
    /* note keys n0..n26: F3..G5 (table from Felucca's HAL comments) */
    {MX_ROW_PA8, 3}, {MX_ROW_PA8, 2}, {MX_ROW_PA8, 5}, {MX_ROW_PA8, 4}, {MX_ROW_PA8, 7},
    {MX_ROW_PA8, 6}, {MX_ROW_PA8, 8}, {MX_ROW_PA8, 9}, {MX_ROW_PA8, 10},
    {MX_ROW_PA7, 0}, {MX_ROW_PA7, 1}, {MX_ROW_PA7, 2}, {MX_ROW_PA7, 3}, {MX_ROW_PA7, 4},
    {MX_ROW_PA7, 5}, {MX_ROW_PA7, 6}, {MX_ROW_PA7, 7}, {MX_ROW_PA7, 8}, {MX_ROW_PA7, 9},
    {MX_ROW_PA7, 10},
    {MX_ROW_PA6, 0}, {MX_ROW_PA6, 1}, {MX_ROW_PA6, 2}, {MX_ROW_PA6, 3}, {MX_ROW_PA6, 4},
    {MX_ROW_PA6, 6}, {MX_ROW_PA6, 5},
};

const mx_pos mx_enc_pos[MX_ENC_COUNT][2] = {
    {{MX_ROW_PA0, 0}, {MX_ROW_PA0, 1}},   /* SELECT */
    {{MX_ROW_PA0, 2}, {MX_ROW_PA0, 3}},   /* ALGORITHM */
    {{MX_ROW_PA5, 8}, {MX_ROW_PA5, 9}},   /* KNOB1 */
    {{MX_ROW_PA0, 8}, {MX_ROW_PA0, 9}},   /* KNOB2 */
    {{MX_ROW_PA0, 6}, {MX_ROW_PA0, 7}},   /* KNOB3 */
    {{MX_ROW_PA0, 4}, {MX_ROW_PA0, 5}},   /* KNOB4 */
    {{MX_ROW_PB7, 0}, {MX_ROW_PB7, 1}},   /* PRESETS */
};

MX_RAM_CODE uint16_t mx_shift_word(uint8_t col)
{
    uint16_t w = (uint16_t)(0xFFFFu & ~(1u << col));
    if (col < 2) w &= (uint16_t)~(1u << (11 + col));
    return w;
}

void mx_init(mx_state *s)
{
    int i;
    memset(s, 0, sizeof *s);
    memset(s->btn_at, -1, sizeof s->btn_at);
    for (i = 0; i < MX_BTN_COUNT; i++)
        s->btn_at[mx_btn_pos[i].col][mx_btn_pos[i].row] = (int8_t)i;
    memcpy(s->encpos, mx_enc_pos, sizeof s->encpos);
}

MX_RAM_CODE static void push(mx_state *s, uint8_t type, uint8_t id, int8_t delta, uint8_t flags)
{
    uint8_t next = (uint8_t)((s->q_head + 1) % MX_QUEUE);
    if (next == s->q_tail) { s->dropped++; return; }
    s->q[s->q_head].type = type;
    s->q[s->q_head].id = id;
    s->q[s->q_head].delta = delta;
    s->q[s->q_head].flags = flags;
    s->q_head = next;
}

int mx_pop(mx_state *s, mx_event *e)
{
    if (s->q_tail == s->q_head) return 0;
    *e = s->q[s->q_tail];
    s->q_tail = (uint8_t)((s->q_tail + 1) % MX_QUEUE);
    return 1;
}

/* quadrature: index = prev << 2 | cur, state = A << 1 | B; +1 clockwise, -1 counter-
 * clockwise, 0 otherwise. Clockwise = B closes first (00 -> 01 -> 11 -> 10 -> 00): the
 * direction Felucca's hardware-tested decoder counts as + and fm1-emu turns for
 * `SELECT:+1`. Verify on hardware. */
static int8_t qtab[16] = { 0, 1, -1, 0,  -1, 0, 0, 1,  1, 0, 0, -1,  0, -1, 1, 0 };   /* RAM, not const */

MX_RAM_CODE static int bit(const mx_state *s, mx_pos p) { return (s->raw[p.col] >> p.row) & 1; }

MX_RAM_CODE static void end_frame(mx_state *s)
{
    int c, r, i;
    s->frames++;
    /* buttons and keys: integrating debounce with hysteresis */
    for (c = 0; c < MX_COLS; c++) {
        for (r = 0; r < MX_ROWS; r++) {
            int id = s->btn_at[c][r];
            uint8_t closed = (s->raw[c] >> r) & 1, was = (s->pressed[c] >> r) & 1;
            if (id < 0) continue;
            if (closed) { if (s->cnt[c][r] < MX_DEBOUNCE) s->cnt[c][r]++; }
            else        { if (s->cnt[c][r] > 0) s->cnt[c][r]--; }
            if (!was && s->cnt[c][r] == MX_DEBOUNCE) {
                s->pressed[c] |= (uint8_t)(1u << r);
                s->held[id] = 0;
                s->hold_sent[id] = 0;
                push(s, MX_EV_BTN_DOWN, (uint8_t)id, 0, 0);
            } else if (was && s->cnt[c][r] == 0) {
                s->pressed[c] &= (uint8_t)~(1u << r);
                push(s, MX_EV_BTN_UP, (uint8_t)id, 0, s->hold_sent[id] ? MX_UP_WAS_HELD : 0);
            } else if (was) {
                if (s->held[id] < 0xFFFF) s->held[id]++;
                if (!s->hold_sent[id] && s->held[id] >= MX_HOLD_FRAMES) {
                    s->hold_sent[id] = 1;
                    push(s, MX_EV_BTN_HOLD, (uint8_t)id, 0, 0);
                }
            }
        }
    }
    /* encoders: on the raw samples, one step per return to the rest state. The rest
     * state is wherever the encoder stays for MX_REST_FRAMES (learned continuously, so
     * parts resting at 00 or at 11, and half- or full-cycle detents, all work). */
    for (i = 0; i < MX_ENC_COUNT; i++) {
        uint8_t st = (uint8_t)((bit(s, s->encpos[i][0]) << 1) | bit(s, s->encpos[i][1]));
        uint8_t prev = s->enc[i].state;
        if (st == prev) {
            if (s->enc[i].stable < MX_REST_FRAMES && ++s->enc[i].stable == MX_REST_FRAMES) {
                s->enc[i].rest = st;
                s->enc[i].acc = 0;
            }
            continue;
        }
        s->enc[i].stable = 0;
        s->enc[i].acc = (int8_t)(s->enc[i].acc + qtab[(prev << 2) | st]);
        s->enc[i].state = st;
        if (st == s->enc[i].rest) {
            if (s->enc[i].acc >= 2) push(s, MX_EV_ENC, (uint8_t)i, 1, 0);
            else if (s->enc[i].acc <= -2) push(s, MX_EV_ENC, (uint8_t)i, -1, 0);
            s->enc[i].acc = 0;
        }
    }
}

MX_RAM_CODE void mx_feed_column(mx_state *s, uint8_t col, uint8_t rows_closed)
{
    if (col >= MX_COLS) return;
    s->raw[col] = rows_closed;
    if (col == MX_COLS - 1) end_frame(s);
}

void mx_led_set(mx_state *s, enum mx_btn id, int on)
{
    mx_pos p;
    if ((unsigned)id >= MX_BTN_COUNT) return;
    p = mx_btn_pos[id];
    if (p.row < MX_ROW_PA5 || p.row > MX_ROW_PA8) return;
    if (on) s->led[p.col] |= (uint8_t)(1u << p.row);
    else    s->led[p.col] &= (uint8_t)~(1u << p.row);
}

int mx_pressed(const mx_state *s, enum mx_btn id)
{
    mx_pos p;
    if ((unsigned)id >= MX_BTN_COUNT) return 0;
    p = mx_btn_pos[id];
    return (s->pressed[p.col] >> p.row) & 1;
}
