/* SPDX-License-Identifier: MIT */
/* FM-1 panel: ST7789-class 240x240 IPS on SPI1 (PC9 CLK, PC10 DO), CS PC7, D/C PC8,
 * backlight PA2 active low (facts from Felucca's hal/fm1_lcd_hw.h and src/lcd.c).
 * SPI1 driven at register level (lcd.c says why), CS, D/C and backlight as GPIOs. RGB565, top-left origin. */
#pragma once
#include "typedef.h"

#define LCD_W 240
#define LCD_H 240

int  lcd_init(void);                                    /* 0 = ok */
void lcd_fill(u16 x, u16 y, u16 w, u16 h, u16 rgb565);
/* draw text with the built-in font at a character cell position (col, row) */
void lcd_text(u16 col, u16 row, const char *s, u16 fg, u16 bg);
/* the same with the 16x28 font (15 columns, 8 rows; the Perform page) */
#define LCD_BIG_COLS 15
#define LCD_BIG_ROWS 8
void lcd_text_big(u16 col, u16 row, const char *s, u16 fg, u16 bg);
void lcd_backlight(int on);                             /* PA2: also the key matrix's /OE, so only ever on */
void lcd_sleep(int on);                                 /* the screen off (panel sleep) and back on, ~130 ms */
void lcd_invert(int on);
u32  lcd_spi_timeouts(void);                            /* transfers that never finished (0 on a healthy panel) */                                /* 0 = the panel's native IPS look */

#define RGB565(r, g, b) ((u16)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
