/* SPDX-License-Identifier: MIT */
/* FM-1 panel: ST7789-class 240x240 IPS on SPI1 (PC9 CLK, PC10 DO), CS PC7, D/C PC8,
 * backlight PA2 active low (facts from Felucca's hal/fm1_lcd_hw.h and src/lcd.c).
 * Uses the SDK SPI device "spi1" and GPIO. RGB565, top-left origin. */
#pragma once
#include "typedef.h"

#define LCD_W 240
#define LCD_H 240

int  lcd_init(void);                                    /* 0 = ok */
void lcd_fill(u16 x, u16 y, u16 w, u16 h, u16 rgb565);
/* draw text with the built-in font at a character cell position (col, row) */
void lcd_text(u16 col, u16 row, const char *s, u16 fg, u16 bg);
void lcd_backlight(int on);
void lcd_invert(int on);                                /* 0 = the panel's native IPS look */

#define RGB565(r, g, b) ((u16)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
