/* SPDX-License-Identifier: MIT */
/* TRS MIDI IN at register level: UART1 RX on PH8 through input channel 1, 31250 baud,
 * the UART's RX DMA filling a ring on its own. The set-up is Felucca's hal/fm1_uart.h
 * (it runs on real FM-1s; the mux and clock values are its), plus the RX and timeout
 * interrupts: the ISR hands the new bytes to the bridge (bridge_trs_rx) as they come, so
 * nothing polls and nothing blocks. The SDK's uart device is not used (why: bridge.c).
 *
 * Registers (docs/hardware.md): CON0 bit 0 on, 3 RX IE, 5 timeout IE, 6 RX DMA, 7 latch
 * the count into HRXCNT, 10 / 12 clear the timeout / RX pending, 11 / 14 the pendings;
 * RXSADR..RXEADR the ring, RXCNT bytes per RX interrupt, OTCNT the timeout in UART clocks.
 * The ISR lives in flash like the SDK's own; the matrix ISR is in RAM because it runs at
 * 5 kHz, this one only when a byte arrives.
 *
 * Verify on hardware: the pin mux and the clock select; a TRS keyboard's notes reach USB
 * and BLE within a few ms; nothing on the jack, no interrupts (System > Statistics). */
#include "system/includes.h"
#include "app_config.h"
#include "asm/gpio.h"
#include "trs_uart.h"
#include "bridge.h"

#define UT1        JL_UART1
#define CLK_CON1   (*(volatile u32 *)0x10010u)     /* [11:10] UART clock: 1 = PLL48M */
#define IOMAP2     (*(volatile u32 *)0x51024u)     /* input channel 1 source [13:8] */
#define IOMAP3     (*(volatile u32 *)0x51028u)     /* UT1: bit 7 fixed IO, [6:4] RX select */
#define RING       256
#define OT_CLOCKS  48000u                          /* 1 ms of silence at 48 MHz */

static u8 ring[RING] __attribute__((aligned(4)));
static u32 rd;                                     /* the first byte not handed over yet */

static ___interrupt void trs_isr(void)
{
    u32 con = UT1->CON0, n;
    if (!(con & (BIT(14) | BIT(11)))) return;      /* neither RX nor timeout pending */
    UT1->CON0 = (u16)(con | BIT(7) | BIT(12) | BIT(10));   /* latch the count, clear both */
    n = UT1->HRXCNT & 0xFFFFu;                     /* bytes DMA'd since the previous latch */
    if (n > RING) n = RING;                        /* overrun: only what the ring still holds */
    while (n) {
        u32 k = RING - rd;
        if (k > n) k = n;
        bridge_trs_rx(ring + rd, k);
        rd = (rd + k) % RING;
        n -= k;
    }
}

int trs_uart_init(void)
{
    UT1->CON0 = 0x3400;                            /* off, pendings cleared */
    UT1->CON1 = 0;
    CLK_CON1 = (CLK_CON1 & ~(3u << 10)) | (1u << 10);
    gpio_direction_input(IO_PORTH_08);
    gpio_set_pull_up(IO_PORTH_08, 1);
    gpio_set_pull_down(IO_PORTH_08, 0);
    gpio_set_die(IO_PORTH_08, 1);
    IOMAP3 &= ~0x80u;
    IOMAP3 = (IOMAP3 & ~0x70u) | (5u << 4);        /* UT1 RX = input channel 1 */
    IOMAP2 = (IOMAP2 & ~0x3F00u) | (49u << 8);     /* channel 1 = PH8 */
    rd = 0;
    UT1->RXSADR = (u32)ring;
    UT1->RXEADR = (u32)ring + RING;
    UT1->RXCNT = 1;                                /* an RX interrupt per byte */
    UT1->OTCNT = OT_CLOCKS;
    UT1->BAUD = (u16)(48000000u / 31250u / 4u - 1u);   /* 383 */
    request_irq(IRQ_UART1_IDX, 2, trs_isr, 0);
    UT1->CON0 = BIT(6) | BIT(7) | BIT(12) | BIT(10) | BIT(3) | BIT(5);   /* RX DMA, latch, clear, RX + timeout IE */
    UT1->CON0 |= BIT(0);                           /* on, last */
    return 0;
}
