/* SPDX-License-Identifier: MIT */
#include "system/includes.h"
#include "app_config.h"
#include "os/os_api.h"
#include "asm/spi.h"
#include "asm/gpio.h"
#include "device/device.h"
#include "lcd.h"
#include "font.h"

#define LOG_TAG_CONST       APP
#define LOG_TAG             "[LCD]"
#define LOG_ERROR_ENABLE
#define LOG_INFO_ENABLE
#include "debug.h"

#define PIN_CS   IO_PORTC_07
#define PIN_DC   IO_PORTC_08
#define PIN_BL   IO_PORTA_02

static void *spi;
static u8 strip[LCD_W * 2 * FONT_H] __attribute__((aligned(4)));   /* one text row, RGB565 */

static void cmd(u8 c)
{
    gpio_direction_output(PIN_DC, 0);
    gpio_direction_output(PIN_CS, 0);
    dev_write(spi, &c, 1);
    gpio_direction_output(PIN_CS, 1);
}

static void data(const void *p, u32 n)
{
    if (!n) return;
    gpio_direction_output(PIN_DC, 1);
    gpio_direction_output(PIN_CS, 0);
    dev_write(spi, (void *)p, n);
    gpio_direction_output(PIN_CS, 1);
}

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

void lcd_invert(int on)
{
    cmd(on ? 0x20 : 0x21);                         /* INVOFF / INVON (INVON is normal for this IPS) */
}

void lcd_backlight(int on)
{
    gpio_direction_output(PIN_BL, on ? 0 : 1);      /* active low */
}

/* ST7789V init: reset, sleep out, 16-bit colour, scan direction, IPS inversion,
 * normal mode, display on. Voltage and gamma stay at the panel defaults. */
int lcd_init(void)
{
    static const u8 b55 = 0x55, b00 = 0x00;
    spi = dev_open("spi1", NULL);
    if (!spi) { log_error("spi1 open failed"); return -1; }
    gpio_direction_output(PIN_CS, 1);
    gpio_direction_output(PIN_DC, 0);
    cmd(0x01); os_time_dly(15);                    /* SWRESET, 150 ms */
    cmd(0x11); os_time_dly(12);                    /* SLPOUT, 120 ms */
    cmd(0x3A); data(&b55, 1);                      /* COLMOD RGB565 */
    cmd(0x36); data(&b00, 1);                      /* MADCTL top-left, RGB */
    cmd(0x21);                                     /* INVON: IPS */
    cmd(0x13);                                     /* NORON */
    lcd_fill(0, 0, LCD_W, LCD_H, 0);
    cmd(0x29);                                     /* DISPON */
    lcd_backlight(1);
    return 0;
}
