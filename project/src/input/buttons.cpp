#include "input/buttons.h"

#include "board_pins.h"
#include "hardware/gpio.h"
#include "input/joystick.h"

static_assert(JOY_UP == (1u << BTN_IDX_UP) && JOY_LEFT == (1u << BTN_IDX_LEFT) &&
                  JOY_RIGHT == (1u << BTN_IDX_RIGHT) && JOY_DOWN == (1u << BTN_IDX_DOWN),
              "JoyDir bits must match ButtonIndex UP/LEFT/RIGHT/DOWN");

namespace {

struct GpioButton {
    uint8_t index;
    uint8_t pin;
};

const GpioButton gpio_buttons[] = {
    {BTN_IDX_X, BTN_PIN_X},
    {BTN_IDX_Y, BTN_PIN_Y},
    {BTN_IDX_A, BTN_PIN_A},
    {BTN_IDX_B, BTN_PIN_B},
};

}

void buttons_init() {
    for (const GpioButton &b : gpio_buttons) {
        gpio_init(b.pin);
        gpio_set_dir(b.pin, GPIO_IN);
        gpio_pull_up(b.pin);
    }
    joystick_init();
}

ButtonMask buttons_read(bool fresh) {
    ButtonMask mask = joystick_read(fresh);
    for (const GpioButton &b : gpio_buttons) {
        if (!gpio_get(b.pin)) {
            mask |= (ButtonMask)(1u << b.index);
        }
    }
    return mask;
}
