#pragma once

// Visual SPI-rate test (build with -DGAME_SPI_TEST=ON). Cycles through SPI rates
// and shows a stress pattern with the rate on screen; the USB log prints the
// requested and actual rate. A clean picture = the panel/wiring accepts it.
[[noreturn]] void spi_test_run();
