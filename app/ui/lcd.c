/* SPDX-License-Identifier: MIT */
/* The panel at register level: SPI1 on its port C group (PC9 CLK, PC10 DO; IOMAP CON1 bit 4),
 * CS PC7, D/C PC8 and the backlight PA2 as plain GPIOs. The SDK's "spi1" device was tried
 * first and left the panel untouched on the first unit (2026-10-19: the stock picture stayed
 * on screen through our SWRESET and the fill); Felucca, SLOOP and BackBeat drive the same
 * controller with these registers and these values (hal/fm1_lcd_hw.h: CON 0x4021, BAUD 4 =
 * 12 MHz, a byte through BUF, a block through ADR/CNT, done when CON bit 15, cleared with
 * bit 14), and BackBeat showed its UI on this unit the same day. The first register-level
 * build (same day) never came up on USB: its init handed the SPI DMA the address of a
 * `const` byte, which lives in flash behind the SFC cache, where no peripheral DMA may read.
 * So every payload goes out of RAM (small ones through a bounce buffer), a transfer waits a
 * bounded time, and after three transfers that never finish the panel is given up for this
 * boot (the UI keeps running without it, `lcd_spi_timeouts` says so). A second register-level
 * build, which also opened the SDK's "spi1" device first and flushed the data cache, reset the
 * chip twice at init (software resets: an exception, not a hang) before the boot guard skipped
 * the panel; both calls are gone, this is Felucca's path and nothing else, and the init
 * announces its stages to the health record so a crash names its step in the next boot's
 * log. Verify on hardware. */
#include "system/includes.h"
#include "app_config.h"
#include "os/os_api.h"
#include "asm/gpio.h"
#include "fm1_log.h"
#include "health.h"
#include "lcd.h"
#include "font.h"
#include "font_big.h"

#define LOG_TAG_CONST       APP
#define LOG_TAG             "[LCD]"
#define LOG_ERROR_ENABLE
#define LOG_INFO_ENABLE
#include "debug.h"

#define PIN_CS   IO_PORTC_07
#define PIN_DC   IO_PORTC_08
#define PIN_CLK  IO_PORTC_09
#define PIN_DO   IO_PORTC_10
#define PIN_BL   IO_PORTA_02
#define SPI      JL_SPI1
#define SPI_DONE BIT(15)
#define SPI_CLR  BIT(14)
#define SPI_CON_RUN 0x4021u          /* enable, mode as Felucca; clears the pending too */

static u8 strip[LCD_W * 2 * FONT_H] __attribute__((aligned(4)));   /* one text row, RGB565 */
static u8 bounce[16] __attribute__((aligned(4)));                   /* small payloads: never DMA from flash */
static u32 spi_timeouts;
static u8 dead;                                                      /* the panel gave no sign of life */

#define WAIT_MS 50u                  /* a 6720-byte strip takes 4.5 ms at 12 MHz */

static int spi_wait(void)
{
    u32 t0 = timer_get_ms();
    while (!(SPI->CON & SPI_DONE)) {
        if (timer_get_ms() - t0 > WAIT_MS) {
            spi_timeouts++;
            if (spi_timeouts == 3) { dead = 1; fm1_log("lcd: three transfers never finished, panel off for this boot"); }
            SPI->CON |= SPI_CLR;
            return -1;
        }
    }
    SPI->CON |= SPI_CLR;
    return 0;
}

static void cmd(u8 c)
{
    if (dead) return;
    gpio_direction_output(PIN_DC, 0);
    gpio_direction_output(PIN_CS, 0);
    SPI->CON |= SPI_CLR;
    SPI->BUF = c;
    spi_wait();
    gpio_direction_output(PIN_CS, 1);
}

static void data(const void *p, u32 n)
{
    if (!n || dead) return;
    if (n <= sizeof bounce) { memcpy(bounce, p, n); p = bounce; }
    gpio_direction_output(PIN_DC, 1);
    gpio_direction_output(PIN_CS, 0);
    SPI->CON |= SPI_CLR;
    SPI->ADR = (u32)p;
    SPI->CNT = n;
    spi_wait();
    gpio_direction_output(PIN_CS, 1);
}

u32 lcd_spi_timeouts(void) { return spi_timeouts; }

static void window(u16 x0, u16 y0, u16 x1, u16 y1)
{
    u8 b[4];
    b[0] = (u8)(x0 >> 8); b[1] = (u8)x0; b[2] = (u8)(x1 >> 8); b[3] = (u8)x1;
    cmd(0x2A); data(b, 4);
    b[0] = (u8)(y0 >> 8); b[1] = (u8)y0; b[2] = (u8)(y1 >> 8); b[3] = (u8)y1;
    cmd(0x2B); data(b, 4);
    cmd(0x2C);
}

void lcd_fill(u16 x, u16 y, u16 w, u16 h, u16 c)
{
    u32 i, rows_per_strip = sizeof strip / (w * 2u);
    u16 *px = (u16 *)strip;
    if (!w || !h) return;
    if (rows_per_strip == 0) rows_per_strip = 1;
    for (i = 0; i < rows_per_strip * w; i++) px[i] = (u16)((c << 8) | (c >> 8));   /* panel wants big endian */
    window(x, y, (u16)(x + w - 1), (u16)(y + h - 1));
    while (h) {
        u16 n = h > rows_per_strip ? (u16)rows_per_strip : h;
        data(strip, (u32)n * w * 2u);
        h = (u16)(h - n);
    }
}

void lcd_text(u16 col, u16 row, const char *s, u16 fg, u16 bg)
{
    u16 *px = (u16 *)strip;
    u16 fgb = (u16)((fg << 8) | (fg >> 8)), bgb = (u16)((bg << 8) | (bg >> 8));
    u32 len = strlen(s), c, y, x;
    u32 maxc = LCD_W / FONT_W - col;
    if (col >= LCD_W / FONT_W || row >= LCD_H / FONT_H) return;
    if (len > maxc) len = maxc;
    if (!len) return;
    for (c = 0; c < len; c++) {
        u8 ch = (u8)s[c];
        const uint8_t *g = font_glyphs[(ch >= FONT_FIRST && ch <= FONT_LAST) ? ch - FONT_FIRST : 0];
        for (y = 0; y < FONT_H; y++)
            for (x = 0; x < FONT_W; x++)
                px[y * (len * FONT_W) + c * FONT_W + x] = (g[y] & (0x80u >> x)) ? fgb : bgb;
    }
    window((u16)(col * FONT_W), (u16)(row * FONT_H), (u16)(col * FONT_W + len * FONT_W - 1), (u16)(row * FONT_H + FONT_H - 1));
    data(strip, len * FONT_W * FONT_H * 2u);
}

/* The big font is drawn in two passes of FONT_BIG_H / 2 pixel rows so the strip
 * (sized for one small text row, 240 x 14 pixels) is enough. */
void lcd_text_big(u16 col, u16 row, const char *s, u16 fg, u16 bg)
{
    u16 *px = (u16 *)strip;
    u16 fgb = (u16)((fg << 8) | (fg >> 8)), bgb = (u16)((bg << 8) | (bg >> 8));
    u32 len = strlen(s), c, y, x, half;
    u32 maxc = LCD_W / FONT_BIG_W - col;
    const u32 rows = FONT_BIG_H / 2;
    if (col >= LCD_W / FONT_BIG_W || row >= LCD_H / FONT_BIG_H) return;
    if (len > maxc) len = maxc;
    if (!len) return;
    for (half = 0; half < 2; half++) {
        for (c = 0; c < len; c++) {
            u8 ch = (u8)s[c];
            const uint16_t *g = font_big_glyphs[(ch >= FONT_BIG_FIRST && ch <= FONT_BIG_LAST) ? ch - FONT_BIG_FIRST : 0];
            for (y = 0; y < rows; y++)
                for (x = 0; x < FONT_BIG_W; x++)
                    px[y * (len * FONT_BIG_W) + c * FONT_BIG_W + x] = (g[half * rows + y] & (0x8000u >> x)) ? fgb : bgb;
        }
        window((u16)(col * FONT_BIG_W), (u16)(row * FONT_BIG_H + half * rows),
               (u16)(col * FONT_BIG_W + len * FONT_BIG_W - 1), (u16)(row * FONT_BIG_H + half * rows + rows - 1));
        data(strip, len * FONT_BIG_W * rows * 2u);
    }
}

void lcd_invert(int on)
{
    cmd(on ? 0x20 : 0x21);                         /* INVOFF / INVON (INVON is normal for this IPS) */
}

void lcd_backlight(int on)
{
    gpio_direction_output(PIN_BL, on ? 0 : 1);      /* active low; never off while running: lcd_sleep is the screen-off */
}

/* Screen off and on through the panel, PA2 untouched: PA2 is also the key matrix's 595 /OE
 * (Felucca src/lcd.c, found on their 1.1.5; found again here on the first unit, 2026-10-19: with
 * the "backlight" off no button, key or knob read and nothing could wake the screen). The panel
 * keeps its RAM asleep; it wants 120 ms after SLPIN before SLPOUT and 120 ms after SLPOUT
 * before DISPON. */
static u32 slept_ms;
void lcd_sleep(int on)
{
    if (on) {
        cmd(0x28);                                 /* DISPOFF */
        cmd(0x10);                                 /* SLPIN */
        slept_ms = timer_get_ms();
    } else {
        u32 since = timer_get_ms() - slept_ms;
        if (since < 120) os_time_dly((120 - since + 9) / 10);
        cmd(0x11);                                 /* SLPOUT */
        os_time_dly(12);
        cmd(0x29);                                 /* DISPON */
    }
}

/* ST7789V init: reset, sleep out, 16-bit colour, scan direction, IPS inversion,
 * normal mode, display on. Voltage and gamma stay at the panel defaults. */
int lcd_init(void)
{
    static const u8 b55 = 0x55, b00 = 0x00;
    /* stages: the health record keeps the last one announced, the next boot's log shows it */
    health_stage(1);
    JL_IOMAP->CON1 |= BIT(4);                      /* SPI1 on the port C group */
    gpio_direction_output(PIN_CS, 1);
    gpio_direction_output(PIN_DC, 0);
    gpio_direction_output(PIN_CLK, 0);
    gpio_direction_output(PIN_DO, 0);
    SPI->CON = SPI_CON_RUN;
    SPI->BAUD = 4;                                 /* 12 MHz */
    health_stage(2);
    cmd(0x01); os_time_dly(15);                    /* SWRESET, 150 ms: the first byte through BUF */
    cmd(0x11); os_time_dly(12);                    /* SLPOUT, 120 ms */
    health_stage(3);
    cmd(0x3A); data(&b55, 1);                      /* COLMOD RGB565: the first DMA, one byte from RAM */
    cmd(0x36); data(&b00, 1);                      /* MADCTL top-left, RGB */
    cmd(0x21);                                     /* INVON: IPS */
    cmd(0x13);                                     /* NORON */
    health_stage(4);
    lcd_fill(0, 0, LCD_W, LCD_H, 0);               /* the first big DMA: 6720-byte strips */
    health_stage(5);
    cmd(0x29);                                     /* DISPON */
    lcd_backlight(1);
    health_stage(6);
    return dead ? -1 : 0;
}
