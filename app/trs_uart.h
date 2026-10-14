/* SPDX-License-Identifier: MIT */
/* TRS MIDI IN: UART1 RX at register level (trs_uart.c). Its interrupt hands every byte
 * to bridge_trs_rx(). */
#pragma once
#include "typedef.h"

int trs_uart_init(void);                /* 0 = receiving */
