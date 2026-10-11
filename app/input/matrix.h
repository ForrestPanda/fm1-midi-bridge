/* SPDX-License-Identifier: MIT */
/* FM-1 front panel: 14 buttons, 7 encoders and 27 note keys in an 11-column x 6-row
 * diode matrix behind two chained 74HC595 shift registers. This file is the portable
 * decoder (host-testable): the platform feeds one column of row samples per tick
 * (matrix_port.c), the decoder debounces, decodes the encoders and queues events for
 * the UI task. Positions are tables because they may differ between hardware batches.
 *
 * Column select word (16 bits, MSB first into the 595 chain): 0xFFFF with bit col
 * cleared; for columns 0 and 1 also bit 11+col cleared; bits 13..15 always high.
 * Row samples: bit set = contact closed (the port reads low). Row bit order below.
 */
#pragma once
#include <stdint.h>

#define MX_COLS 11
enum { MX_ROW_PA0 = 0, MX_ROW_PA5, MX_ROW_PA6, MX_ROW_PA7, MX_ROW_PA8, MX_ROW_PB7, MX_ROWS };

enum mx_btn {
    MX_BTN_OCT_MINUS, MX_BTN_OCT_PLUS, MX_BTN_FX, MX_BTN_SCL, MX_BTN_ENV, MX_BTN_LFO,
    MX_BTN_EDIT, MX_BTN_GLO, MX_BTN_HOME, MX_BTN_SAVE, MX_BTN_ARP, MX_BTN_SEQ,
    MX_BTN_PLAY, MX_BTN_REC,
    MX_BTN_KEY0,                        /* 27 note keys, F3..G5, follow */
    MX_BTN_COUNT = MX_BTN_KEY0 + 27
};
enum mx_enc { MX_ENC_SELECT, MX_ENC_ALGO, MX_ENC_KNOB1, MX_ENC_KNOB2, MX_ENC_KNOB3, MX_ENC_KNOB4, MX_ENC_PRESETS, MX_ENC_COUNT };

typedef struct { uint8_t row, col; } mx_pos;
extern const mx_pos mx_btn_pos[MX_BTN_COUNT];
extern const mx_pos mx_enc_pos[MX_ENC_COUNT][2];       /* A, B */

enum { MX_EV_BTN_DOWN, MX_EV_BTN_UP, MX_EV_BTN_HOLD, MX_EV_ENC };
#define MX_UP_WAS_HELD 1
typedef struct { uint8_t type, id; int8_t delta; uint8_t flags; } mx_event;

#define MX_DEBOUNCE    8        /* frames (~1.1 ms each) to accept a press or a release */
#define MX_HOLD_FRAMES 640      /* ~700 ms */
#define MX_REST_FRAMES 40       /* unchanged frames that make a state the encoder's rest */
#define MX_QUEUE       32

typedef struct {
    uint8_t  raw[MX_COLS];                      /* last sample per column, bit = row */
    uint8_t  cnt[MX_COLS][MX_ROWS];             /* debounce counters */
    uint8_t  pressed[MX_COLS];                  /* debounced state */
    uint16_t held[MX_BTN_COUNT];                /* frames pressed */
    uint8_t  hold_sent[MX_BTN_COUNT];
    struct { uint8_t state, rest, stable; int8_t acc; } enc[MX_ENC_COUNT];
    mx_pos   encpos[MX_ENC_COUNT][2];           /* RAM copy of mx_enc_pos for the ISR path */
    uint8_t  led[MX_COLS];                      /* bits = MX_ROW_* (only PA5..PA8 have LEDs) */
    volatile uint8_t q_head, q_tail;
    mx_event q[MX_QUEUE];
    uint32_t frames, dropped;
    int8_t   btn_at[MX_COLS][MX_ROWS];          /* reverse table: button id or -1 */
} mx_state;

void     mx_init(mx_state *s);
void     mx_feed_column(mx_state *s, uint8_t col, uint8_t rows_closed);   /* per tick; col MX_COLS-1 ends a frame */
int      mx_pop(mx_state *s, mx_event *e);                                 /* consumer side; 1 = got one */
void     mx_led_set(mx_state *s, enum mx_btn id, int on);
int      mx_pressed(const mx_state *s, enum mx_btn id);
uint16_t mx_shift_word(uint8_t col);
