/* Host test of the panel matrix decoder. */
#include "../app/input/matrix.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static mx_state s;
static uint8_t cells[MX_COLS];           /* closed mask per column we feed */
static void set_cell(mx_pos p, int on) { if (on) cells[p.col] |= (uint8_t)(1u << p.row); else cells[p.col] &= (uint8_t)~(1u << p.row); }
static void frame(void) { int c; for (c = 0; c < MX_COLS; c++) mx_feed_column(&s, (uint8_t)c, cells[c]); }
static void frames(int n) { while (n--) frame(); }
static int events(mx_event *out, int max) { int n = 0; while (n < max && mx_pop(&s, &out[n])) n++; return n; }
static void enc_state(int e, int a, int b) { set_cell(mx_enc_pos[e][0], a); set_cell(mx_enc_pos[e][1], b); }

int main(void)
{
    mx_event ev[8]; int n, i;
    CHECK(mx_shift_word(0) == 0xF7FE && mx_shift_word(1) == 0xEFFD && mx_shift_word(5) == 0xFFDF && mx_shift_word(10) == 0xFBFF);
    mx_init(&s); memset(cells, 0, sizeof cells);
    frames(20);                                           /* encoders learn their rest state */
    CHECK(events(ev, 8) == 0);
    /* press, hold below threshold, release */
    set_cell(mx_btn_pos[MX_BTN_HOME], 1); frames(MX_DEBOUNCE - 1); CHECK(events(ev, 8) == 0);
    frames(1); n = events(ev, 8); CHECK(n == 1 && ev[0].type == MX_EV_BTN_DOWN && ev[0].id == MX_BTN_HOME);
    CHECK(mx_pressed(&s, MX_BTN_HOME));
    set_cell(mx_btn_pos[MX_BTN_HOME], 0); frames(MX_DEBOUNCE);
    n = events(ev, 8); CHECK(n == 1 && ev[0].type == MX_EV_BTN_UP && ev[0].flags == 0);
    /* bounce: 3 frames closed then open: nothing */
    set_cell(mx_btn_pos[MX_BTN_FX], 1); frames(3); set_cell(mx_btn_pos[MX_BTN_FX], 0); frames(10);
    CHECK(events(ev, 8) == 0);
    /* hold */
    set_cell(mx_btn_pos[MX_BTN_OCT_PLUS], 1); frames(MX_DEBOUNCE + MX_HOLD_FRAMES + 2);
    n = events(ev, 8); CHECK(n == 2 && ev[0].type == MX_EV_BTN_DOWN && ev[1].type == MX_EV_BTN_HOLD && ev[1].id == MX_BTN_OCT_PLUS);
    set_cell(mx_btn_pos[MX_BTN_OCT_PLUS], 0); frames(MX_DEBOUNCE);
    n = events(ev, 8); CHECK(n == 1 && ev[0].type == MX_EV_BTN_UP && ev[0].flags == MX_UP_WAS_HELD);
    /* two keys in one column decode independently (ENV col 2 PA5 and n1 col 2 PA8) */
    set_cell(mx_btn_pos[MX_BTN_ENV], 1); set_cell(mx_btn_pos[MX_BTN_KEY0 + 1], 1); frames(MX_DEBOUNCE);
    n = events(ev, 8); CHECK(n == 2 && ev[0].type == MX_EV_BTN_DOWN && ev[1].type == MX_EV_BTN_DOWN && ev[0].id != ev[1].id);
    set_cell(mx_btn_pos[MX_BTN_ENV], 0); set_cell(mx_btn_pos[MX_BTN_KEY0 + 1], 0); frames(MX_DEBOUNCE); events(ev, 8);
    /* encoder: full cycle clockwise 00 -> 10 -> 11 -> 01 -> 00 = +1 */
    enc_state(MX_ENC_SELECT, 1, 0); frame(); enc_state(MX_ENC_SELECT, 1, 1); frame();
    enc_state(MX_ENC_SELECT, 0, 1); frame(); enc_state(MX_ENC_SELECT, 0, 0); frame();
    n = events(ev, 8); CHECK(n == 1 && ev[0].type == MX_EV_ENC && ev[0].id == MX_ENC_SELECT && ev[0].delta == 1);
    /* counter-clockwise = -1 */
    enc_state(MX_ENC_SELECT, 0, 1); frame(); enc_state(MX_ENC_SELECT, 1, 1); frame();
    enc_state(MX_ENC_SELECT, 1, 0); frame(); enc_state(MX_ENC_SELECT, 0, 0); frame();
    n = events(ev, 8); CHECK(n == 1 && ev[0].delta == -1);
    /* half cycle and back: nothing */
    enc_state(MX_ENC_SELECT, 1, 0); frame(); enc_state(MX_ENC_SELECT, 0, 0); frame();
    CHECK(events(ev, 8) == 0);
    /* bounce on A inside a detent: still one step */
    enc_state(MX_ENC_SELECT, 1, 0); frame(); enc_state(MX_ENC_SELECT, 0, 0); frame(); enc_state(MX_ENC_SELECT, 1, 0); frame();
    enc_state(MX_ENC_SELECT, 1, 1); frame(); enc_state(MX_ENC_SELECT, 0, 1); frame(); enc_state(MX_ENC_SELECT, 0, 0); frame();
    n = events(ev, 8); CHECK(n == 1 && ev[0].delta == 1);
    /* a part that rests at 11: stays there long enough to be learned, then 11 -> 01 -> 00 -> 10 -> 11 */
    enc_state(MX_ENC_KNOB1, 1, 1); frames(MX_REST_FRAMES + 2); events(ev, 8);
    CHECK(s.enc[MX_ENC_KNOB1].rest == 3);
    enc_state(MX_ENC_KNOB1, 0, 1); frame(); enc_state(MX_ENC_KNOB1, 0, 0); frame();
    enc_state(MX_ENC_KNOB1, 1, 0); frame(); enc_state(MX_ENC_KNOB1, 1, 1); frame();
    n = events(ev, 8); CHECK(n == 1 && ev[0].id == MX_ENC_KNOB1 && ev[0].delta == 1);
    /* queue overflow drops and counts */
    for (i = 0; i < 40; i++) { set_cell(mx_btn_pos[MX_BTN_REC], 1); frames(MX_DEBOUNCE); set_cell(mx_btn_pos[MX_BTN_REC], 0); frames(MX_DEBOUNCE); }
    CHECK(s.dropped > 0); n = events(ev, 8); CHECK(n == 8);
    while (mx_pop(&s, ev)) ;
    /* LEDs */
    mx_led_set(&s, MX_BTN_FX, 1); CHECK(s.led[6] == (1u << MX_ROW_PA5));
    mx_led_set(&s, MX_BTN_FX, 0); CHECK(s.led[6] == 0);
    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("matrix tests passed\n");
    return 0;
}
