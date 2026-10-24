/* SPDX-License-Identifier: MIT */
/* The AC791N's USB key from an STM32F4-Discovery (STM32F407, 16 MHz HSI, bare metal, no libc).
 * Sends the 16-bit key 0x16EF MSB first on the FM-1's USB data lines: D+ (green) the clock on
 * PD0, D- (white) the data on PD1, GND to GND, nothing on VBUS (the FM-1 runs on its battery).
 * Bit timing after ip2k's dongle spec (docs/10-usb-key-dongle.md) and kurogedelic's
 * FM-1-transporter (both: D+ clock, "polarity A"): per bit the clock low 8 us, data set 3 us
 * after the falling edge, clock high 12 us (the ROM latches on the rising edge, ~50 kHz).
 * The sequence is czietz's, blind, in a loop (main() says why no acknowledge is looked for):
 * 1.5 s of key packets (green LED), 2 s of a 1 kHz square wave on D+ (all four LEDs), then 4 s
 * with the lines released (blue). If D+ stays high all through the released time (a live bus)
 * the board stops for good, blue blinking; otherwise the next round starts.
 * Verified on the first unit (2026-10-19, docs/recovery.md): the board keying, the FM-1 switched
 * on; when the LEDs go from all four to blue, unplug the board and plug the FM-1 into the computer
 * right away: the ROM shows as USB disk 4c4a:8057 (on the second power-on there). A swap long after
 * the wave lost the ROM (it boots flash without a host): if no disk shows, power the FM-1 off, plug
 * the board back and try again. The board's reset button starts it over.
 * Lines are driven push-pull at 3.3 V as FM-1-transporter does, released = input.
 * gap_ack() and sof() are the acknowledge-watching variant, kept unused: without pull-ups the
 * acknowledge cannot be told from an idle bus. */
#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(a))
#define RCC_AHB1ENR   REG(0x40023830u)
#define GPIOD_MODER   REG(0x40020C00u)
#define GPIOD_OTYPER  REG(0x40020C04u)
#define GPIOD_OSPEEDR REG(0x40020C08u)
#define GPIOD_PUPDR   REG(0x40020C0Cu)
#define GPIOD_IDR     REG(0x40020C10u)
#define GPIOD_BSRR    REG(0x40020C18u)
#define DEMCR         REG(0xE000EDFCu)
#define DWT_CTRL      REG(0xE0001000u)
#define DWT_CYCCNT    REG(0xE0001004u)

#define DP 0u              /* PD0: D+ */
#define DM 1u              /* PD1: D- */
#define LED_GREEN 12u
#define LED_ORANGE 13u
#define LED_RED 14u
#define LED_BLUE 15u
#define CPU_HZ 16000000u

static void delay_us(uint32_t us)
{
    uint32_t t0 = DWT_CYCCNT, n = us * (CPU_HZ / 1000000u);
    while (DWT_CYCCNT - t0 < n);
}
static uint32_t now_us(void) { return DWT_CYCCNT / (CPU_HZ / 1000000u); }

static void pin_out(uint32_t p) { GPIOD_MODER = (GPIOD_MODER & ~(3u << (2 * p))) | (1u << (2 * p)); }
static void pin_in(uint32_t p)  { GPIOD_MODER &= ~(3u << (2 * p)); }
static void pin_hi(uint32_t p)  { GPIOD_BSRR = 1u << p; }
static void pin_lo(uint32_t p)  { GPIOD_BSRR = 1u << (16 + p); }
static int  pin_rd(uint32_t p)  { return (GPIOD_IDR >> p) & 1u; }
static void leds(uint32_t mask) { GPIOD_BSRR = (0xF000u << 16) | (mask & 0xF000u); }

static void release(void) { pin_in(DP); pin_in(DM); }

/* one 16-bit packet, push-pull, then release both lines */
static void packet(uint16_t key)
{
    int i;
    pin_hi(DP); pin_lo(DM); pin_out(DP); pin_out(DM);
    delay_us(5);
    for (i = 15; i >= 0; i--) {
        pin_lo(DP);                              /* falling edge */
        delay_us(3);
        if (key & (1u << i)) pin_hi(DM); else pin_lo(DM);
        delay_us(5);
        pin_hi(DP);                              /* rising edge: the ROM latches */
        delay_us(12);
    }
    release();
}

/* both lines low for >= 100 us during the gap = the acknowledge */
static int __attribute__((unused)) gap_ack(uint32_t gap_us)
{
    uint32_t t0 = now_us(), low_since = 0, have = 0;
    while (now_us() - t0 < gap_us) {
        if (!pin_rd(DP) && !pin_rd(DM)) {
            if (!have) { have = 1; low_since = now_us(); }
            else if (now_us() - low_since >= 100u) return 1;
        } else have = 0;
        delay_us(10);
    }
    return 0;
}

/* 4 us low pulses on D+ every 1 ms until D+ stays low 3 ms or the limit; 1 = calibration seen */
static int __attribute__((unused)) sof(uint32_t limit_ms)
{
    uint32_t t0 = now_us(), low_since = 0, have = 0;
    pin_in(DP);
    while (now_us() - t0 < 500000u && !pin_rd(DP));   /* the chip pulls D+ up within ~2 ms */
    while (now_us() - t0 < limit_ms * 1000u) {
        uint32_t t = now_us();
        pin_lo(DP); pin_out(DP); delay_us(4); pin_in(DP);
        while (now_us() - t < 1000u) {
            if (!pin_rd(DP)) {
                if (!have) { have = 1; low_since = now_us(); }
                else if (now_us() - low_since >= 3000u) return 1;
            } else have = 0;
            delay_us(20);
        }
    }
    return 0;
}

static void blind_sof(uint32_t ms)          /* czietz: 1 kHz square wave, no acknowledge checked */
{
    uint32_t t0 = now_us();
    pin_out(DP);
    while (now_us() - t0 < ms * 1000u) { pin_lo(DP); delay_us(500); pin_hi(DP); delay_us(500); }
    release();
}

static void pause_ms(uint32_t ms) { uint32_t t0 = now_us(); while (now_us() - t0 < ms * 1000u); }

/* D+ high without a break for the whole time = a live USB bus (the ROM attached and the computer
 * enumerated it, or a firmware that runs): the board must stay quiet from then on */
static int bus_alive(uint32_t ms)
{
    uint32_t t0 = now_us();
    while (now_us() - t0 < ms * 1000u) {
        if (!pin_rd(DP)) return 0;
        delay_us(50);
    }
    return 1;
}

int main(void)
{
    uint32_t i;
    RCC_AHB1ENR |= 1u << 3;                      /* GPIOD clock */
    DEMCR |= 1u << 24; DWT_CYCCNT = 0; DWT_CTRL |= 1u;
    GPIOD_OSPEEDR |= (3u << (2 * DP)) | (3u << (2 * DM));
    GPIOD_PUPDR &= ~((3u << (2 * DP)) | (3u << (2 * DM)));
    for (i = 12; i < 16; i++) pin_out(i);
    release();
    /* Blind, czietz's way, in a loop: without pull-ups on the lines the acknowledge (both lines
     * low) cannot be told from the idle bus (both lines low too: the chip's and the computer's
     * pull-downs), so no acknowledge is looked for. Each round: 1.5 s of key packets, 2 s of the
     * 1 kHz calibration wave, then 4 s released. If the ROM took the key, it attaches after the
     * wave (D+ pulled up) and the computer enumerates it; the board sees D+ high throughout the
     * released time and stays quiet for good (blue blinking). A boot-looping firmware shows D+
     * high only in bursts, so the rounds go on and one catches its next reset. */
    for (;;) {
        uint32_t t0 = now_us();
        leds(1u << LED_GREEN);
        while (now_us() - t0 < 1500000u) { packet(0x16EFu); delay_us(160); }
        leds(0xF000u);
        blind_sof(2000);
        release();
        leds(1u << LED_BLUE);
        if (bus_alive(4000)) {
            for (;;) { leds(1u << LED_BLUE); pause_ms(500); leds(0); pause_ms(500); }   /* reset button to key again */
        }
    }
}

/* ---- bare-metal start-up: vector table, data/bss, main ---- */
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
void Reset_Handler(void)
{
    uint32_t *s = &_sidata, *d = &_sdata;
    while (d < &_edata) *d++ = *s++;
    for (d = &_sbss; d < &_ebss;) *d++ = 0;
    main();
    for (;;);
}
static void Default_Handler(void) { for (;;); }
__attribute__((section(".isr_vector"), used))
const void *vectors[] = { &_estack, Reset_Handler, Default_Handler, Default_Handler, Default_Handler, Default_Handler, Default_Handler };
