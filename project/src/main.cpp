#include "audio/sound.h"
#include "board/clock.h"
#include "board/fault.h"
#include "board/spi_bus.h"
#include "display/ili9341.h"
#include "game/game_loop.h"
#include "game/spi_test.h"
#include "input/buttons.h"
#include "pico/stdlib.h"

#ifndef GAME_SPI_TEST
#define GAME_SPI_TEST 0
#endif
#ifndef GAME_SYS_CLOCK_KHZ
#define GAME_SYS_CLOCK_KHZ 0
#endif

int main() {
    fault_boot_capture();
    const bool fallback = clock_guard_begin();
    clock_crumb(1);
    clock_init(fallback ? 0 : GAME_SYS_CLOCK_KHZ);
    clock_crumb(2);
    game_set_boot_info(fallback);
    stdio_init_all();
    clock_crumb(3);
    lcd_pins_init();
    spi_bus_init();
    buttons_init();
    clock_crumb(4);
    sound_init();
    clock_crumb(5);
    sound_request(1u << SND_MENU_START);
    lcd_init();
    clock_crumb(6);
#if GAME_SPI_TEST
    spi_test_run();
#endif
    game_splash();
    clock_crumb(7);
    game_init();
    clock_crumb(8);
    game_run();
}
