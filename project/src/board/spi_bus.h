#pragma once

#include <cstdint>

constexpr uint32_t SPI_BUS_INIT_HZ = 40 * 1000 * 1000;

void spi_bus_init();
