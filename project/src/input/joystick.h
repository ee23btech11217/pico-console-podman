#pragma once

#include <cstdint>

#ifndef JOY_OVERSAMPLE
#define JOY_OVERSAMPLE 1  // ADC conversions averaged per axis per sample
#endif

enum JoyDir : uint8_t {
    JOY_UP = 1u << 0,
    JOY_LEFT = 1u << 1,
    JOY_RIGHT = 1u << 2,
    JOY_DOWN = 1u << 3,
};

void joystick_init();
uint8_t joystick_read(bool force = false);

// Peak-to-peak raw ADC jitter and mean cost of one sample since the last call.
void joystick_stats_take(uint16_t &ppx, uint16_t &ppy, uint32_t &avg_us, uint32_t &samples);
// Analog axes in the sim's orientation: x +right, y +down, -127..127 (0 in the deadzone)
void joystick_axes(int8_t &x, int8_t &y);
uint16_t joystick_raw_x();
uint16_t joystick_raw_y();
