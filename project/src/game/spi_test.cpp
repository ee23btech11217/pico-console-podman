#include "game/spi_test.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "board/clock.h"
#include "display/ili9341.h"
#include "hardware/clocks.h"
#include "hardware/spi.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "shapoco/gfx2d/fonts.hpp"
#include "shapoco/gfx2d/graphics2d.hpp"

namespace g2 = shapoco::gfx2d;

namespace {

alignas(4) uint16_t g_buf[2][LCD_W * LCD_BAND_H];

inline uint16_t sw(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }

void draw_band(uint16_t *px, int y0, const char *label) {
    for (int y = 0; y < LCD_BAND_H; ++y) {
        const int Y = y0 + y;
        for (int x = 0; x < LCD_W; ++x) {
            uint16_t c;
            if (Y < 60) {  // colour bars + 1-pixel alternating columns (worst-case data toggling)
                const int bar = x * 8 / LCD_W;
                static const uint16_t bars[8] = {0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800, 0x001F, 0x0000};
                c = ((x & 1) && Y >= 30) ? (uint16_t)~bars[bar] : bars[bar];
            } else if (Y < 120) {  // gradients
                c = (uint16_t)(((x * 31 / LCD_W) << 11) | (((Y - 60) * 63 / 60) << 5) | (31 - x * 31 / LCD_W));
            } else if (Y < 180) {  // checkerboard 2x2
                c = (((x >> 1) ^ (Y >> 1)) & 1) ? 0xFFFF : 0x0000;
            } else {  // fine diagonal
                c = ((x + Y) & 1) ? 0xFFFF : 0x0000;
            }
            px[y * LCD_W + x] = sw(c);
        }
    }
    const g2::Surface surf = g2::makeSurface(g2::PixelFormat::RGB565_SWAPPED, LCD_W, LCD_BAND_H, px);
    g2::Graphics2D g(surf);
    g.setClipRect(0, 0, LCD_W, LCD_BAND_H);
    g.setFont(&g2::ShapoSansMono_s08c07);
    for (int row = 0; row < 3; ++row) {
        const int ty = 72 + row * 36 - y0;
        if (ty > -12 && ty < LCD_BAND_H) {
            g.fillRect(8, ty - 2, 200, 12, g2::makeColor(0, 0, 0));
            g.setTextColor(g2::makeColor(255, 255, 0));
            g.drawString(10, ty, label);
        }
    }
}

}  // namespace

void spi_test_run() {
    // (core clock kHz, SPI request Hz): the SPI divider is clk_peri / (2k), so the
    // intermediate rates need their own core clock.
    struct Step { uint32_t sys_khz; uint32_t spi_hz; };
    const Step steps[] = {{300000, 75000000u}, {320000, 80000000u}, {360000, 90000000u}, {300000, 50000000u}};
    char label[48];
    for (;;) {
        for (const Step &st : steps) {
            if (set_sys_clock_khz(st.sys_khz, false)) {
                clock_configure_undivided(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                                          clock_get_hz(clk_sys));
            }
            lcd_set_spi_hz(st.spi_hz);
            spi_set_baudrate(spi0, st.spi_hz);
            const uint32_t act = spi_get_baudrate(spi0);
            snprintf(label, sizeof(label), "SPI %lu.%lu MHZ  (sys %lu MHZ)", (unsigned long)(act / 1000000),
                     (unsigned long)((act / 100000) % 10), (unsigned long)(clock_get_hz(clk_sys) / 1000000));
            printf("SPI-TEST sys %lu request %lu actual %lu Hz\n", (unsigned long)clock_get_hz(clk_sys),
                   (unsigned long)st.spi_hz, (unsigned long)act);
            const uint32_t t0 = time_us_32();
            uint32_t frames = 0;
            while (time_us_32() - t0 < 6000000u) {
                for (int b = 0; b < LCD_H / LCD_BAND_H; ++b) {
                    uint16_t *px = g_buf[b & 1];
                    draw_band(px, b * LCD_BAND_H, label);
                    lcd_wait_idle();
                    lcd_write_band_start(b * LCD_BAND_H, LCD_BAND_H, px);
                }
                lcd_wait_idle();
                ++frames;
                clock_guard_feed();
            }
            printf("SPI-TEST %lu Hz: %lu frames in 6 s\n", (unsigned long)act, (unsigned long)frames);
        }
    }
}
