/* SPDX-License-Identifier: MIT */
#include "tester_uart.h"
#include "stm32f4xx.h"

#define APB1_HZ 42000000u
#define TX_SIZE 4096u                    /* a power of two; free-running indices */

static volatile uint8_t  tx_buf[TX_SIZE];
static volatile uint32_t tx_head, tx_tail;

static void pa2_af(int af)               /* the UART's TX, or a plain output driven low */
{
    GPIOA->MODER = (GPIOA->MODER & ~(3u << 4)) | ((af ? 2u : 1u) << 4);
}

void tuart_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    __DSB();
    GPIOA->AFR[0] = (GPIOA->AFR[0] & ~(0xFu << 8)) | (7u << 8);   /* PA2 AF7 */
    GPIOA->OSPEEDR |= 1u << 4;
    GPIOA->BSRR = 1u << (2 + 16);                                 /* the break level, for later */
    pa2_af(1);
    /* PA3 high is the loop's source (TRS ring), so the jack needs no 3V wire: tip (sink) on PA2,
     * ring on PA3; the FM-1's MIDI IN has its own series resistors */
    GPIOA->BSRR = 1u << 3;
    GPIOA->MODER = (GPIOA->MODER & ~(3u << 6)) | (1u << 6);
    USART2->BRR = APB1_HZ / 31250u;                               /* 1344, exact */
    USART2->CR1 = USART_CR1_TE | USART_CR1_UE;
    NVIC_SetPriority(USART2_IRQn, 1);
    NVIC_EnableIRQ(USART2_IRQn);
}

void USART2_IRQHandler(void)
{
    if ((USART2->CR1 & USART_CR1_TXEIE) && (USART2->SR & USART_SR_TXE)) {
        if (tx_tail != tx_head) {
            USART2->DR = tx_buf[tx_tail & (TX_SIZE - 1)];
            tx_tail++;
        } else {
            USART2->CR1 &= ~USART_CR1_TXEIE;
        }
    }
}

uint32_t tuart_free(void) { return TX_SIZE - (tx_head - tx_tail); }

uint32_t tuart_queued(void)
{
    return (tx_head - tx_tail) + ((USART2->SR & USART_SR_TC) ? 0u : 1u);
}

uint32_t tuart_write(const uint8_t *p, uint32_t n)
{
    uint32_t i = 0;
    while (i < n && tx_head - tx_tail < TX_SIZE) {
        tx_buf[tx_head & (TX_SIZE - 1)] = p[i++];
        tx_head++;
    }
    if (i) {
        __disable_irq();
        USART2->CR1 |= USART_CR1_TXEIE;
        __enable_irq();
    }
    return i;
}

int tuart_idle(void) { return tx_head == tx_tail && (USART2->SR & USART_SR_TC); }

void tuart_baud(int32_t off)
{
    uint64_t num = (uint64_t)APB1_HZ * 10000u;
    uint64_t den = (uint64_t)31250u * (uint64_t)(10000 + off);
    USART2->CR1 &= ~USART_CR1_UE;
    USART2->BRR = (uint32_t)((num + den / 2) / den);
    USART2->CR1 |= USART_CR1_UE;
}

void tuart_break_us(uint32_t us)
{
    uint32_t t0 = TIM2->CNT;
    pa2_af(0);
    while (TIM2->CNT - t0 < us) {}
    pa2_af(1);
}
