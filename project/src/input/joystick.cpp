#include "input/joystick.h"

#include "board_pins.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "pico/time.h"

#ifndef JOY_PIN_VRX
#define JOY_PIN_VRX 26
#endif
#ifndef JOY_PIN_VRY
#define JOY_PIN_VRY 27
#endif
#ifndef JOY_ON_THRESH
#define JOY_ON_THRESH 1000
#endif
#ifndef JOY_OFF_THRESH
#define JOY_OFF_THRESH 700
#endif
#ifndef JOY_DEADZONE
#define JOY_DEADZONE 150
#endif
#ifndef JOY_FULL
#define JOY_FULL 1600
#endif
#ifndef JOY_INVERT_UD
#define JOY_INVERT_UD 1  // Round 16 board: up/down reversed against the old board (user)
#endif
#ifndef JOY_POLL_US
#define JOY_POLL_US 2000
#endif

static_assert(JOY_PIN_VRX >= 26 && JOY_PIN_VRX <= 29, "VRx must be on an ADC pin (GPIO26-29)");
static_assert(JOY_PIN_VRY >= 26 && JOY_PIN_VRY <= 29, "VRy must be on an ADC pin (GPIO26-29)");
static_assert(JOY_PIN_VRX != JOY_PIN_VRY, "VRx and VRy must be different pins");

namespace {

constexpr int ADC_CH_X = JOY_PIN_VRX - 26;
constexpr int ADC_CH_Y = JOY_PIN_VRY - 26;
constexpr int CENTER_NOMINAL = 2048;

int g_centerX = CENTER_NOMINAL, g_centerY = CENTER_NOMINAL;
int g_stateX = 0, g_stateY = 0;
uint16_t g_rawX = CENTER_NOMINAL, g_rawY = CENTER_NOMINAL;
uint8_t g_dir = 0;
int8_t g_axisX = 0, g_axisY = 0;  // sim orientation, -127..127
uint32_t g_lastUs = 0;

void sample(uint16_t &x, uint16_t &y) {
    uint32_t sx = 0, sy = 0;
    adc_select_input(ADC_CH_X);
    (void)adc_read();  // first conversion after the mux switch settles the source
    for (int i = 0; i < JOY_OVERSAMPLE; ++i) sx += adc_read();
    adc_select_input(ADC_CH_Y);
    (void)adc_read();
    for (int i = 0; i < JOY_OVERSAMPLE; ++i) sy += adc_read();
    x = (uint16_t)(sx / JOY_OVERSAMPLE);
    y = (uint16_t)(sy / JOY_OVERSAMPLE);
}

uint16_t g_minX = 4095, g_maxX = 0, g_minY = 4095, g_maxY = 0;
uint32_t g_costUs = 0, g_samples = 0;

// Counts from centre -> -127..127 with a deadzone; full scale at JOY_FULL.
int8_t analog_axis(int d) {
    const int a = d < 0 ? -d : d;
    if (a <= JOY_DEADZONE) return 0;
    int v = (a - JOY_DEADZONE) * 127 / (JOY_FULL - JOY_DEADZONE);
    if (v > 127) v = 127;
    return (int8_t)(d < 0 ? -v : v);
}

int axis_state(int d, int prev) {
    if (d > JOY_ON_THRESH) return 1;
    if (d < -JOY_ON_THRESH) return -1;
    if (prev > 0 && d > JOY_OFF_THRESH) return 1;
    if (prev < 0 && d < -JOY_OFF_THRESH) return -1;
    return 0;
}

}  // namespace

void joystick_init() {
    adc_init();
    adc_gpio_init(JOY_PIN_VRX);
    adc_gpio_init(JOY_PIN_VRY);

    constexpr int N = 32;
    int sx = 0, sy = 0;
    for (int i = 0; i < N; ++i) {
        uint16_t x, y;
        sample(x, y);
        sx += x;
        sy += y;
    }
    const int cx = sx / N, cy = sy / N;
    if (cx > 1400 && cx < 2700) g_centerX = cx;
    if (cy > 1400 && cy < 2700) g_centerY = cy;
    g_lastUs = time_us_32();
}

uint8_t joystick_read(bool force) {
    const uint32_t now = time_us_32();
    if (!force && (uint32_t)(now - g_lastUs) < (uint32_t)JOY_POLL_US) return g_dir;
    g_lastUs = now;

    uint16_t rx, ry;
    sample(rx, ry);
    g_costUs += time_us_32() - now;
    ++g_samples;
    if (rx < g_minX) g_minX = rx;
    if (rx > g_maxX) g_maxX = rx;
    if (ry < g_minY) g_minY = ry;
    if (ry > g_maxY) g_maxY = ry;
    g_rawX = rx;
    g_rawY = ry;
    const int dx = (int)rx - g_centerX, dy = (int)ry - g_centerY;
    g_stateY = axis_state(dy, g_stateY);
    // Old board: X+ = UP (sim y negative), Y+ = LEFT (sim x negative). JOY_INVERT_UD flips up/down (analog + digital).
    const int ud = JOY_INVERT_UD ? -dx : dx;
    g_stateX = axis_state(ud, g_stateX);
    g_axisX = analog_axis(-dy);
    g_axisY = analog_axis(-ud);

    uint8_t dir = 0;
    if (g_stateX > 0) dir |= JOY_UP;
    if (g_stateX < 0) dir |= JOY_DOWN;
    if (g_stateY > 0) dir |= JOY_LEFT;
    if (g_stateY < 0) dir |= JOY_RIGHT;
    g_dir = dir;
    return dir;
}

void joystick_axes(int8_t &x, int8_t &y) {
    x = g_axisX;
    y = g_axisY;
}
uint16_t joystick_raw_x() { return g_rawX; }
uint16_t joystick_raw_y() { return g_rawY; }

void joystick_stats_take(uint16_t &ppx, uint16_t &ppy, uint32_t &avg_us, uint32_t &samples) {
    ppx = g_samples ? (uint16_t)(g_maxX - g_minX) : 0;
    ppy = g_samples ? (uint16_t)(g_maxY - g_minY) : 0;
    samples = g_samples;
    avg_us = g_samples ? g_costUs / g_samples : 0;
    g_minX = g_minY = 4095;
    g_maxX = g_maxY = 0;
    g_costUs = g_samples = 0;
}
