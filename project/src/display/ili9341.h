#pragma once

#include <cstddef>
#include <cstdint>

#ifndef LCD_LANDSCAPE
#define LCD_LANDSCAPE 1
#endif

#ifndef LCD_MADCTL
#if LCD_LANDSCAPE
#define LCD_MADCTL 0xA0  // ST7789 board (Round 16): MY|MV, RGB order; the ILI9341 used 0xE8 (MY|MX|MV|BGR)
#else
#define LCD_MADCTL 0x48
#endif
#endif

#ifndef LCD_INVERT
#define LCD_INVERT 0  // INVON off: the user saw correct colours without it (Round 16)
#endif

#ifndef LCD_BSWAP
#define LCD_BSWAP 1  // DMA byte swap: the framebuffer is stored byte-swapped, 0 sends noise (Round 16)
#endif

#if LCD_LANDSCAPE
constexpr int LCD_W = 320;
constexpr int LCD_H = 240;
#else
constexpr int LCD_W = 240;
constexpr int LCD_H = 320;
#endif
#ifndef LCD_BAND_H_CFG
#define LCD_BAND_H_CFG 40
#endif
constexpr int LCD_BAND_H = LCD_BAND_H_CFG;

#ifndef LCD_SPI_HZ
#define LCD_SPI_HZ 40000000u
#endif

// Runtime SPI clock request (default LCD_SPI_HZ); takes effect on the next band.
void lcd_set_spi_hz(uint32_t hz);
uint32_t lcd_spi_hz();
void lcd_pins_init();
void lcd_init();
void lcd_set_backlight(bool on);
// Panel sleep: DISPOFF + SLPIN and the backlight off (the panel keeps its picture in GRAM); lcd_wake() is SLPOUT (120 ms), DISPON, backlight on.
void lcd_sleep();
void lcd_wake();
void lcd_write_band_start(int y, int h, const uint16_t *pixels);
void lcd_wait_idle();
