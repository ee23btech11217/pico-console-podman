#include "board/clock.h"

#include "hardware/clocks.h"
#include "hardware/regs/watchdog.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/watchdog.h"
#include "hardware/vreg.h"
#include "hardware/watchdog.h"
#include "hardware/sync.h"
#include "pico/runtime_init.h"
#include "pico/stdlib.h"

#ifndef GAME_COPY_TO_RAM_CFG
#define GAME_COPY_TO_RAM_CFG 0
#endif
#ifndef GAME_WDT_MS
#define GAME_WDT_MS 3000
#endif
#ifndef GAME_VREG_OVERRIDE
#define GAME_VREG_OVERRIDE 0
#endif

namespace {

constexpr uint32_t ARMED = 0xC10C4ED1u;
uint32_t g_reason = 0;
uint64_t g_firstFeedUs = 0;
uint32_t g_prevCrumb = 0;
uint32_t g_s0 = 0;
uint32_t g_sAll[8];

}  // namespace

bool clock_guard_begin() {
    g_reason = watchdog_hw->reason;
    g_prevCrumb = watchdog_hw->scratch[1];
    const bool wdtKilled = (g_reason & WATCHDOG_REASON_TIMER_BITS) != 0;
    for (int i = 0; i < 8; i++) g_sAll[i] = watchdog_hw->scratch[i];
    g_s0 = watchdog_hw->scratch[0];
    const bool unproven = g_s0 == ARMED;
    watchdog_hw->scratch[0] = ARMED;
    watchdog_enable(GAME_WDT_MS, true);
    return wdtKilled && unproven;
}

void clock_guard_feed() {
    watchdog_update();
    const uint64_t now = time_us_64();
    if (g_firstFeedUs == 0) g_firstFeedUs = now;
    if (now - g_firstFeedUs > 5000000u) watchdog_hw->scratch[0] = 0;
}

void clock_crumb(uint32_t n) { watchdog_hw->scratch[1] = n; }
uint32_t clock_prev_crumb() { return g_prevCrumb; }

uint32_t clock_qmi_timing() { return qmi_hw->m[0].timing; }
uint32_t clock_wdt_reason() { return g_reason; }
uint32_t clock_boot_scratch(int i) { return g_sAll[i & 7]; }
uint32_t clock_boot_scratch0() { return g_s0; }

#ifndef GAME_FLASH_DIV
#define GAME_FLASH_DIV 0  // 0 = automatic: keep the boot value, raise it to stay <= 100 MHz
#endif

static void flash_div_set(uint32_t div) {
    uint32_t t = qmi_hw->m[0].timing;
    t = (t & ~QMI_M0_TIMING_CLKDIV_BITS) | (div << QMI_M0_TIMING_CLKDIV_LSB);
    qmi_hw->m[0].timing = t;
}
static uint32_t flash_div_get() {
    return (qmi_hw->m[0].timing & QMI_M0_TIMING_CLKDIV_BITS) >> QMI_M0_TIMING_CLKDIV_LSB;
}

static uint32_t g_requestedKhz = 0;

// Sleep (README Round 9e): slow the system clock through the PLL (crystal + PLL keep running, voltage unchanged) and
// bring back exactly the clock that ran before. The peripheral clock follows the system clock as in clock_init().
static uint32_t g_beforeSleepKhz = 0;
static enum vreg_voltage g_beforeSleepV = VREG_VOLTAGE_DEFAULT;
#ifndef SLEEP_VREG
#define SLEEP_VREG VREG_VOLTAGE_1_10  // Round 9e step 3: core voltage while asleep (stock value; never lower)
#endif
bool clock_sleep_slow(uint32_t khz) {
    g_beforeSleepKhz = clock_get_hz(clk_sys) / 1000u;
    if (!set_sys_clock_khz(khz, false)) return false;
    clock_configure_undivided(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, clock_get_hz(clk_sys));
    g_beforeSleepV = vreg_get_voltage();
    if (g_beforeSleepV > SLEEP_VREG) vreg_set_voltage(SLEEP_VREG);  // clock first, then voltage
    return true;
}
void clock_sleep_restore() {
    if (!g_beforeSleepKhz) return;
    if (vreg_get_voltage() != g_beforeSleepV) {
        vreg_set_voltage(g_beforeSleepV);  // voltage first, then clock
        busy_wait_us(1000);
    }
    if (set_sys_clock_khz(g_beforeSleepKhz, false)) {
        clock_configure_undivided(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, clock_get_hz(clk_sys));
    }
    g_beforeSleepKhz = 0;
}

void clock_guard_suspend() { watchdog_disable(); }
void clock_guard_rearm() {
    watchdog_enable(GAME_WDT_MS, true);
    g_firstFeedUs = 0;
}

void clock_init(uint32_t sys_khz) {
    g_requestedKhz = sys_khz;
    // The flash is used for code (XIP builds) and always for the high score. Code is fetched from flash (XIP). Raising the QSPI divider is safe at any
    // time, so a larger divider goes in before the PLL moves, a smaller one after.
    const uint32_t cur = flash_div_get();
    const uint32_t final_khz = sys_khz ? sys_khz : 150000;
    uint32_t want = GAME_FLASH_DIV ? (uint32_t)GAME_FLASH_DIV : cur;
    const uint32_t floor_div = (final_khz + 99999u) / 100000u;  // flash <= 100 MHz
    if (!GAME_FLASH_DIV && want < floor_div) want = floor_div;
    if (want >= cur && want != cur) flash_div_set(want);
    if (sys_khz != 0) {
        if (GAME_VREG_OVERRIDE) {
            vreg_set_voltage((enum vreg_voltage)GAME_VREG_OVERRIDE);
            sleep_ms(10);
        } else if (sys_khz > 300000) {  // 1.30 V only for the clocks that need it (see README Round 9)
            vreg_set_voltage(VREG_VOLTAGE_1_30);
            sleep_ms(10);
        } else if (sys_khz > 150000) {
            vreg_set_voltage(VREG_VOLTAGE_1_15);
            sleep_ms(10);
        }
        if (set_sys_clock_khz(sys_khz, false)) {
            clock_configure_undivided(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                                      clock_get_hz(clk_sys));
        }
    }
    if (want < cur) flash_div_set(want);
}

// After a wake from DORMANT the chip runs from the crystal with the PLLs off: rebuild the clock tree like at boot
// (runtime_init_clocks), then the game's own clock sequence (core voltage, flash divider, system PLL, peripheral clock).
void clock_reinit_after_sleep() {
    clock_crumb(30);
    vreg_set_voltage(VREG_VOLTAGE_1_10);  // the default the SDK clock setup expects
    busy_wait_at_least_cycles(2000);      // cycle based: the timer's tick may not be running yet
    clock_crumb(31);
    runtime_init_clocks();
    clock_crumb(32);
    clock_init(g_requestedKhz);
    clock_crumb(33);
}
