/* SPDX-License-Identifier: MIT */
/* TRS MIDI IN: UART1 RX at register level (trs_uart.c). Its interrupt hands every byte
 * to bridge_trs_rx(). */
#pragma once
#include "typedef.h"

int trs_uart_init(void);                /* 0 = receiving */
void trs_uart_counts(u32 *rx_irq, u32 *ot_irq, u32 *bytes, u32 *breaks);   /* interrupts so far by kind, bytes received, line breaks dropped */
u32  trs_uart_con(void);                 /* the CON0 | CON1 << 16 bits seen since the last call, for the log */
