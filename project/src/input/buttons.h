#pragma once

#include <cstdint>

enum ButtonIndex : uint8_t {
    BTN_IDX_UP = 0,
    BTN_IDX_LEFT,
    BTN_IDX_RIGHT,
    BTN_IDX_DOWN,
    BTN_IDX_X,
    BTN_IDX_Y,
    BTN_IDX_A,
    BTN_IDX_B,
    BTN_IDX_COUNT
};

using ButtonMask = uint16_t;

void buttons_init();

// fresh = bypass the joystick's poll-rate limit and sample the ADC right now
ButtonMask buttons_read(bool fresh = false);
