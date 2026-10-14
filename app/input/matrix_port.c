/* SPDX-License-Identifier: MIT */
/* SDK side of the panel matrix: pins, the 74HC595 chain and a 10 kHz TIMER3 interrupt
 * that scans one column per tick (the rows of a column are read on the tick after it
 * was latched, so they settle for 100 us). Everything the ISR touches is in RAM.
 *
 *   SER PA4, SRCLK PA3, RCLK PA1      outputs
 *   rows PA0, PA5, PA6, PA7, PA8, PB7 inputs with pull-up, low = closed
 *   LEDs PA9 (row PA5), PA10 (PA6), PH6 (PA7), PH9 (PA8), active high
 */
#include "system/includes.h"
#include "app_config.h"
#include "asm/clock.h"
#include "asm/gpio.h"
#include "matrix.h"

#define LOG_TAG_CONST       APP
#define LOG_TAG             "[MATRIX]"
#define LOG_ERROR_ENABLE
#define LOG_INFO_ENABLE
#include "debug.h"

/* 100 us a column: 1.1 ms frames, the matrix.h frame counts assume it; the ISR costs
 * under 2% of core 0. Overridable on the make line (`-DMX_TICK_US=200`: then halve the
 * frame counts too). */
#ifndef MX_TICK_US
#define MX_TICK_US 100
#endif
#ifndef MX_IRQ_PRIO
#define MX_IRQ_PRIO 3
#endif

#define PA_SER   BIT(4)
#define PA_SRCLK BIT(3)
#define PA_RCLK  BIT(1)
#define PA_ROWS  (BIT(0) | BIT(5) | BIT(6) | BIT(7) | BIT(8))
#define PB_ROW   BIT(7)
#define PA_LEDS  (BIT(9) | BIT(10))
#define PH_LEDS  (BIT(6) | BIT(9))

static SEC_USED(.volatile_ram) mx_state state;
static SEC_USED(.volatile_ram) u8 cur_col, next_col;
static SEC_USED(.volatile_ram) JL_TIMER_TypeDef *TMR = JL_TIMER3;

#define RAMCODE SEC_USED(.volatile_ram_code)

static RAMCODE void shift_out(u16 word)
{
    int i;
    for (i = 15; i >= 0; i--) {
        if (word & (1u << i)) JL_PORTA->OUT |= PA_SER; else JL_PORTA->OUT &= ~PA_SER;
        JL_PORTA->OUT |= PA_SRCLK;
        JL_PORTA->OUT &= ~PA_SRCLK;
    }
    JL_PORTA->OUT |= PA_RCLK;
    JL_PORTA->OUT &= ~PA_RCLK;
}

static RAMCODE void leds_set(u8 rows)
{
    u32 a = 0, h = 0;
    if (rows & BIT(MX_ROW_PA5)) a |= BIT(9);
    if (rows & BIT(MX_ROW_PA6)) a |= BIT(10);
    if (rows & BIT(MX_ROW_PA7)) h |= BIT(6);
    if (rows & BIT(MX_ROW_PA8)) h |= BIT(9);
    JL_PORTA->OUT = (JL_PORTA->OUT & ~PA_LEDS) | a;
    JL_PORTH->OUT = (JL_PORTH->OUT & ~PH_LEDS) | h;
}

static ___interrupt RAMCODE void mx_isr(void)
{
    u32 pa, pb;
    u8 closed = 0;
    if (!(TMR->CON & BIT(15))) return;
    TMR->CON |= BIT(14);                          /* clear pending */
    pa = JL_PORTA->IN;
    pb = JL_PORTB->IN;
    if (!(pa & BIT(0))) closed |= BIT(MX_ROW_PA0);
    if (!(pa & BIT(5))) closed |= BIT(MX_ROW_PA5);
    if (!(pa & BIT(6))) closed |= BIT(MX_ROW_PA6);
    if (!(pa & BIT(7))) closed |= BIT(MX_ROW_PA7);
    if (!(pa & BIT(8))) closed |= BIT(MX_ROW_PA8);
    if (!(pb & BIT(7))) closed |= BIT(MX_ROW_PB7);
    leds_set(0);
    shift_out(mx_shift_word(next_col));
    leds_set(state.led[next_col]);
    mx_feed_column(&state, cur_col, closed);
    cur_col = next_col;
    next_col = (u8)((next_col + 1) % MX_COLS);
}

static void timer_start(u32 us)
{
    u32 clk = clk_get("timer");
    u32 prd = clk / 1000000u * us;
    u8 psc = 0;
    static const u8 psc_code[16] = {0, 4, 1, 5, 2, 6, 3, 7, 8, 12, 9, 13, 10, 14, 11, 15};
    while (prd > 65535 && psc < 15) { psc++; prd >>= 1; }
    TMR->CON = 0;
    TMR->CNT = 0;
    TMR->CON |= BIT(14);
    TMR->PRD = prd;
    TMR->CON |= (u32)psc_code[psc] << 4;
    request_irq(IRQ_TIMER3_IDX, MX_IRQ_PRIO, mx_isr, 0);
    TMR->CON |= BIT(0);
    log_info("matrix scan: timer clk %u, prd %u psc %u", clk, prd, psc);
}

mx_state *matrix_state(void) { return &state; }

void matrix_init(void)
{
    mx_init(&state);
    gpio_direction_output(IO_PORTA_04, 0);        /* SER */
    gpio_direction_output(IO_PORTA_03, 0);        /* SRCLK */
    gpio_direction_output(IO_PORTA_01, 0);        /* RCLK */
    gpio_direction_output(IO_PORTA_09, 0);        /* LED lines */
    gpio_direction_output(IO_PORTA_10, 0);
    gpio_direction_output(IO_PORTH_06, 0);
    gpio_direction_output(IO_PORTH_09, 0);
    gpio_set_hd(IO_PORTA_09, 1); gpio_set_hd(IO_PORTA_10, 1);
    gpio_set_hd(IO_PORTH_06, 1); gpio_set_hd(IO_PORTH_09, 1);
    {
        static const u8 rows[] = {IO_PORTA_00, IO_PORTA_05, IO_PORTA_06, IO_PORTA_07, IO_PORTA_08, IO_PORTB_07};
        int i;
        for (i = 0; i < 6; i++) {
            gpio_direction_input(rows[i]);
            gpio_set_pull_up(rows[i], 1);
            gpio_set_pull_down(rows[i], 0);
            gpio_set_die(rows[i], 1);
        }
    }
    shift_out(0xFFFF);                            /* nothing selected */
    cur_col = 0; next_col = 1;
    shift_out(mx_shift_word(0));
    timer_start(MX_TICK_US);
}
