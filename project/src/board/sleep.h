#pragma once

#include <cstdint>

// Deep sleep (RP2350 DORMANT): everything is stopped, RAM and both cores keep their state, and a falling edge on
// wakePin restarts the oscillator and the program continues right after the call. Call it only from core0 while
// core1 is idle, with no flash write and no display transfer under way, and with the wake button released.
// Display, speaker, watchdog and clocks are put to sleep and restored here.
void board_sleep_until_pressed(unsigned wakePin);

// What the last sleep did, for the log (USB may be away for a moment after a wake): how many sleeps and which GPIO event woke it.
void board_sleep_info(char *buf, unsigned n);
// True while the console sleeps: core 1 waits in WFE and is released with __sev() after the wake.
bool board_sleep_active();
// Core 1's park loop while asleep (deep sleep in WFE until the wake).
void board_sleep_core1_park();
// Die temperature (ADC4), degrees C.
float board_die_temp_c();
