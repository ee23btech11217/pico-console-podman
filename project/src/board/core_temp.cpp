#include "board/core_temp.h"

#include "board/clock.h"

#include <cstdio>

#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

namespace {
constexpr uint32_t PERIOD_MS = 1000;
constexpr uint32_t SETTLE_US = 2000;  // regulator mode change before the read (README Round 9f)

int16_t g_c10 = INT16_MIN;
CoreTempLevel g_level = CoreTempLevel::UNKNOWN;
char g_text[12] = "CORE --C";
bool g_smpsInit = false;
bool g_waiting = false;  // regulator switched to PWM, reading due
uint32_t g_startUs = 0, g_lastMs = 0;

void smps_pwm(bool on) {
#ifdef PICO_SMPS_MODE_PIN
    if (!g_smpsInit) {
        gpio_init(PICO_SMPS_MODE_PIN);
        gpio_set_dir(PICO_SMPS_MODE_PIN, GPIO_OUT);
        g_smpsInit = true;
    }
    gpio_put(PICO_SMPS_MODE_PIN, on ? 1 : 0);
#else
    (void)on;
#endif
}

// The conversion itself (50 us settle + 20 conversions of 2 us, ~0.1 ms): the regulator must already be in PWM mode.
float convert() {
    adc_set_temp_sensor_enabled(true);
    adc_select_input(4);
    busy_wait_us(50);                               // sensor bias and input settle (it may just have been ungated)
    for (int i = 0; i < 4; i++) (void)adc_read();  // discard the first conversions after the mux switch
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) sum += adc_read();
    const float v = (float)sum / 16.0f * 3.3f / 4096.0f;
    return 27.0f - (v - 0.706f) / 0.001721f;  // datasheet §12.4.6
}

void store(float c) {
    const int c10 = (int)(c * 10.0f + (c >= 0 ? 0.5f : -0.5f));
    g_c10 = (int16_t)c10;
    // Hysteresis only downwards: a level is entered at its threshold and left 2 C below it (strict: never late)
    CoreTempLevel lv;
    if (c10 >= CORE_TEMP_HOT_C10) lv = CoreTempLevel::HOT;
    else if (g_level == CoreTempLevel::HOT && c10 >= CORE_TEMP_HOT_C10 - CORE_TEMP_HYST_C10) lv = CoreTempLevel::HOT;
    else if (c10 >= CORE_TEMP_WARM_C10) lv = CoreTempLevel::WARM;
    else if (g_level >= CoreTempLevel::WARM && c10 >= CORE_TEMP_WARM_C10 - CORE_TEMP_HYST_C10) lv = CoreTempLevel::WARM;
    else lv = CoreTempLevel::OK;
    if (lv != g_level && g_level != CoreTempLevel::UNKNOWN) {
        printf("TEMP level %d -> %d at %d.%d C\n", (int)g_level, (int)lv, c10 / 10, c10 % 10);
    }
    g_level = lv;
    int whole = (c10 + 5) / 10;
    if (whole < 0) whole = 0;
    if (whole > 99) whole = 99;
    snprintf(g_text, sizeof g_text, "%s %2dC", lv == CoreTempLevel::HOT ? "HOT!" : "CORE", whole);
}
}  // namespace

float core_temp_read_now() {
    smps_pwm(true);
    busy_wait_us(SETTLE_US);
    const float c = convert();
    smps_pwm(false);
    g_waiting = false;  // a pending non-blocking read starts over
    store(c);
    return c;
}

void core_temp_poll(uint32_t now_ms) {
    if (g_waiting) {
        if (time_us_32() - g_startUs < SETTLE_US) return;
        const float c = convert();
        smps_pwm(false);
        g_waiting = false;
        store(c);
        return;
    }
    if (g_c10 != INT16_MIN && now_ms - g_lastMs < PERIOD_MS) return;
    g_lastMs = now_ms;
    smps_pwm(true);
    g_startUs = time_us_32();
    g_waiting = true;
}

int16_t core_temp_c10() { return g_c10; }
CoreTempLevel core_temp_level() { return g_level; }
const char *core_temp_text() { return g_text; }

#if GAME_TEMP_TEST
#include "pico/stdio_usb.h"
namespace {
struct RingSample {
    uint16_t s10;  // time since boot, 0.1 s
    int16_t c10;   // temperature, 0.1 C
    uint8_t phase, usb;
};
constexpr unsigned RING_N = 200;
RingSample g_ring[RING_N];
unsigned g_ringHead = 0, g_ringCount = 0;
bool g_usbWas = false;
}  // namespace

void core_temp_ring_add(uint8_t phase, float c) {
    RingSample &r = g_ring[g_ringHead];
    r.s10 = (uint16_t)(time_us_64() / 100000u);
    r.c10 = (int16_t)(c * 10.0f);
    r.phase = phase;
    r.usb = stdio_usb_connected() ? 1 : 0;
    g_ringHead = (g_ringHead + 1) % RING_N;
    if (g_ringCount < RING_N) ++g_ringCount;
}

void core_temp_ring_poll() {
    const bool usb = stdio_usb_connected();
    if (usb && !g_usbWas) {
        sleep_ms(200);  // the terminal has just opened: give it a moment
        printf("TMPRING begin %u samples (s phase C usb)\n", g_ringCount);
        for (unsigned i = 0; i < g_ringCount; ++i) {
            clock_guard_feed();  // the dump takes longer than a frame: keep the watchdog fed
            const RingSample &r = g_ring[(g_ringHead + RING_N - g_ringCount + i) % RING_N];
            printf("TMPRING %u.%u %u %d.%d %u\n", r.s10 / 10u, r.s10 % 10u, r.phase, r.c10 / 10, (r.c10 < 0 ? -r.c10 : r.c10) % 10,
                   r.usb);
        }
        printf("TMPRING end\n");
    }
    g_usbWas = usb;
}
#endif
