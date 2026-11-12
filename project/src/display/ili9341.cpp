#include "display/ili9341.h"

#include "board_pins.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

namespace {

uint32_t g_spiHz = LCD_SPI_HZ;
int g_dma = -1;
bool g_pending = false;

inline void lcd_select(bool selected) { gpio_put(LCD_PIN_CS, selected ? 0 : 1); }
inline void lcd_dc(bool data) { gpio_put(LCD_PIN_DC, data ? 1 : 0); }
inline void spi_format8() { spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST); }
inline void spi_format16() { spi_set_format(spi0, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST); }

void lcd_write_command(uint8_t command) {
    lcd_dc(false);
    lcd_select(true);
    spi_write_blocking(spi0, &command, 1);
    lcd_select(false);
}

void lcd_write_data(const uint8_t *data, size_t length) {
    lcd_dc(true);
    lcd_select(true);
    spi_write_blocking(spi0, data, length);
    lcd_select(false);
}

void lcd_write_u16(uint16_t value) {
    uint8_t data[2] = {(uint8_t)(value >> 8), (uint8_t)(value & 0xff)};
    lcd_write_data(data, 2);
}

void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    lcd_write_command(0x2A);
    lcd_write_u16(x0);
    lcd_write_u16(x1);
    lcd_write_command(0x2B);
    lcd_write_u16(y0);
    lcd_write_u16(y1);
    lcd_write_command(0x2C);
}

}  // namespace

void lcd_set_spi_hz(uint32_t hz) { g_spiHz = hz; }
uint32_t lcd_spi_hz() { return g_spiHz; }

void lcd_pins_init() {
    gpio_init(LCD_PIN_LED);
    gpio_set_dir(LCD_PIN_LED, GPIO_OUT);
    gpio_put(LCD_PIN_LED, 0);
    gpio_init(LCD_PIN_CS);
    gpio_set_dir(LCD_PIN_CS, GPIO_OUT);
    gpio_put(LCD_PIN_CS, 1);
    gpio_init(LCD_PIN_DC);
    gpio_set_dir(LCD_PIN_DC, GPIO_OUT);
    gpio_put(LCD_PIN_DC, 0);
    gpio_init(LCD_PIN_RST);
    gpio_set_dir(LCD_PIN_RST, GPIO_OUT);
    gpio_put(LCD_PIN_RST, 1);
}

void lcd_set_backlight(bool on) { gpio_put(LCD_PIN_LED, on ? 1 : 0); }

void lcd_init() {
    lcd_set_backlight(true);
    sleep_ms(20);
    gpio_put(LCD_PIN_RST, 0);
    sleep_ms(20);
    gpio_put(LCD_PIN_RST, 1);
    sleep_ms(120);
    lcd_write_command(0x01);
    sleep_ms(120);
    lcd_write_command(0x11);
    sleep_ms(120);
    lcd_write_command(0x3A);
    lcd_write_data((const uint8_t[]){0x55}, 1);
#if LCD_INVERT
    lcd_write_command(0x21);  // INVON (ST7789 panel)
#endif
    lcd_write_command(0x36);
    lcd_write_data((const uint8_t[]){(uint8_t)LCD_MADCTL}, 1);
    lcd_write_command(0x29);
    sleep_ms(20);
    g_dma = dma_claim_unused_channel(true);
    spi_set_baudrate(spi0, g_spiHz);
}

void lcd_sleep() {
    lcd_wait_idle();
    lcd_write_command(0x28);  // DISPOFF
    sleep_ms(5);
    lcd_write_command(0x10);  // SLPIN
    sleep_ms(6);
    lcd_set_backlight(false);
}

void lcd_wake() {
    lcd_write_command(0x11);  // SLPOUT
    sleep_ms(120);
    lcd_write_command(0x29);  // DISPON
    sleep_ms(20);
    lcd_set_backlight(true);
}

void lcd_wait_idle() {
    if (!g_pending) return;
    dma_channel_wait_for_finish_blocking(g_dma);
    while (spi_is_busy(spi0)) tight_loop_contents();
    lcd_select(false);
    while (spi_is_readable(spi0)) (void)spi_get_hw(spi0)->dr;
    spi_get_hw(spi0)->icr = SPI_SSPICR_RORIC_BITS;
    spi_format8();
    g_pending = false;
}

void lcd_write_band_start(int y, int h, const uint16_t *pixels) {
    lcd_wait_idle();
    spi_set_baudrate(spi0, g_spiHz);
    lcd_set_window(0, (uint16_t)y, LCD_W - 1, (uint16_t)(y + h - 1));
    spi_format16();
    lcd_dc(true);
    lcd_select(true);
    dma_channel_config c = dma_channel_get_default_config(g_dma);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, spi_get_dreq(spi0, true));
    channel_config_set_bswap(&c, LCD_BSWAP != 0);
    dma_channel_configure(g_dma, &c, &spi_get_hw(spi0)->dr, pixels, (uint32_t)(LCD_W * h), true);
    g_pending = true;
}
