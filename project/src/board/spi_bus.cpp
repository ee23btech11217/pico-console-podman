#include "board/spi_bus.h"

#include "board_pins.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"

void spi_bus_init() {
    gpio_set_function(LCD_PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(LCD_PIN_MOSI, GPIO_FUNC_SPI);
    spi_init(spi0, SPI_BUS_INIT_HZ);
    spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
}
