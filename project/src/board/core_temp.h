#pragma once

#include <cstdint>

// Die temperature of the RP2350 (on-chip sensor, ADC channel 4 on QFN-60, datasheet §12.4.6:
// T = 27 - (V - 0.706) / 0.001721). The Pico 2's 3.3 V regulator is forced into PWM mode (GPIO23 high) around
// every read: in its power-save mode the ripple shifted the reading by up to +11 C (README Round 9f).
// All functions: core 0 only (the joystick shares the ADC there).

// Strict thresholds for the on-screen indicator, in 0.1 C. Datasheet limits: junction -40..125 C absolute max
// (Table 1680), ambient -40..85 C (Table 1682), the regulator's own over-temperature cut-out defaults to 125 C
// (VREG_CTRL.HT_TH). The sensor is only as good as the 3.3 V reference (~4 C per 1 % error, §12.4.6), so the
// warnings sit far below those limits: WARM from 60 C, HOT from 70 C; a level is left 2 C below its threshold.
#ifndef CORE_TEMP_WARM_C10
#define CORE_TEMP_WARM_C10 600
#endif
#ifndef CORE_TEMP_HOT_C10
#define CORE_TEMP_HOT_C10 700
#endif
#define CORE_TEMP_HYST_C10 20

enum class CoreTempLevel : uint8_t { UNKNOWN, OK, WARM, HOT };

// Blocking read (~2.2 ms, regulator settle + 20 conversions). Used by the sleep loop and the temperature test.
float core_temp_read_now();

// Non-blocking reader for the frame loop: call once per frame. Once a second it switches the regulator to PWM
// mode and, on a later frame >= 2 ms after, takes the reading (~50 us) and hands the regulator back.
void core_temp_poll(uint32_t now_ms);

// Last reading in 0.1 C (INT16_MIN before the first one), its level, and an 8-character label ("CORE 64C",
// "HOT! 72C", "CORE --C") for the overlay.
int16_t core_temp_c10();
CoreTempLevel core_temp_level();
const char *core_temp_text();

#if GAME_TEMP_TEST
// Battery temperature run (README Round 10, step 5): every TMP sample also goes into a RAM ring (200 x 6 B), which
// is printed as "TMPRING" lines each time a USB terminal connects, so a run made on battery can be read afterwards.
void core_temp_ring_add(uint8_t phase, float c);
void core_temp_ring_poll();  // core 0, once per frame and in the sleep loop
#endif
