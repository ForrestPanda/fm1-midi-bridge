/* SPDX-License-Identifier: MIT */
/* MIDI OUT on USART2 TX (PA2) for the TRS tester: a 4 KB ring sent by the TXE interrupt, the baud
 * rate settable off nominal, and a line break (PA2 held low as a GPIO). PA3 is driven high: the
 * current loop's source. */
#pragma once
#include <stdint.h>

void     tuart_init(void);
uint32_t tuart_free(void);                       /* room in the ring, bytes */
uint32_t tuart_queued(void);                     /* bytes not yet on the line, the one shifting out included */
uint32_t tuart_write(const uint8_t *p, uint32_t n);
int      tuart_idle(void);                       /* ring empty and the last stop bit sent */
void     tuart_baud(int32_t offset_001pct);      /* 31250 * (1 + offset / 10000) */
void     tuart_break_us(uint32_t us);            /* only when idle: PA2 low for us, back to the UART */
