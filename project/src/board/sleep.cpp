#include "board/sleep.h"

#include <cstdio>

#include "audio/sound.h"
#include "board/clock.h"
#include "board/core_temp.h"
#include "hardware/structs/pads_bank0.h"
#include "display/ili9341.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pll.h"
#include "hardware/powman.h"
#include "hardware/structs/io_bank0.h"
#include "hardware/regs/clocks.h"
#include "hardware/regs/rosc.h"
#include "hardware/rosc.h"
#include "hardware/structs/rosc.h"
#include "hardware/sync.h"
#include "hardware/structs/clocks.h"
#include "hardware/adc.h"
#include "hardware/timer.h"
#include "hardware/irq.h"
#include "hardware/structs/timer.h"
#include "hardware/structs/scb.h"
#include "hardware/vreg.h"
#include "hardware/xosc.h"
#include "board_pins.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

// SLEEP_DORMANT 1 = the original DORMANT sleep (all clocks stopped). It woke after a short sleep but not after a long
// one, and the board then stayed dark and got hot (README Round 9, cause unknown). 0 (default) = light sleep: the
// 12 MHz crystal keeps running, the cores wait in WFI, a timer wakes them every 20 ms to look at B, and the watchdog
// keeps running, so any failure resets the board instead of leaving it stuck.
#ifndef SLEEP_DORMANT
#define SLEEP_DORMANT 0
#endif
#ifndef SLEEP_SYS_KHZ
// System clock while asleep, reached through the PLL (Round 9e). 64 MHz, not 48: RP2350-E12 (datasheet App. D.11.1)
// requires clk_sys >= 1.1 x clk_usb (48 MHz) while USB is in use, or USB status events can be lost (Round 10).
#define SLEEP_SYS_KHZ 64000
#endif
static_assert(SLEEP_SYS_KHZ * 10 >= 48000 * 11, "RP2350-E12: clk_sys must be >= 10 % faster than clk_usb");
// Clock gating while both cores sleep (SLEEP_EN0/1, datasheet §6.5.2). 3 = the wake minimum, as in the
// datasheet's own example (§6.5.2 note, §8.1.6.6): IO/pads (B interrupt), TIMER0 + ticks (1 s alarm), watchdog,
// PSM/resets/clocks/POWMAN, XOSC and both PLLs, USB (always, see below); everything else (bus fabric,
// SRAM, XIP, ROM, SIO, OTP, all other peripherals) is gated, the clock-request handshake turns it back on before
// a core touches the bus. Step back if a wake fails: 2 = also bus fabric + SRAM0-9, 1 = also XIP + ROM + SIO,
// 0 = the Round 9e mask (only unused peripherals gated).
#ifndef SLEEP_GATE_LEVEL
#define SLEEP_GATE_LEVEL 0  // Round 10 tests 1-3: levels 3 and 2 did not wake after > ~10 s (README Round 10)
#endif
#ifndef SLEEP_SCREEN_ONLY
#define SLEEP_SCREEN_ONLY 1  // default (README Round 9): screen + sound off only, clocks/voltage/USB untouched; the deep
                             // variants (clock + voltage shutdown) did not wake after a long sleep and the board got hot
#endif


namespace {
uint32_t g_count = 0, g_ms = 0, g_cause = 0;
volatile bool g_asleep = false;  // read by core 1 (board_sleep_active)
[[maybe_unused]] uint32_t g_pinAtWake = 0;  // g_ms: the always-on timer does not count in this state, unused

#if !SLEEP_DORMANT && !SLEEP_SCREEN_ONLY
// Interrupts are masked here (PRIMASK): a pending alarm still ends WFI, its handler never runs.
void light_sleep_until_low(unsigned pin) {
    const unsigned alarm = (unsigned)hardware_alarm_claim_unused(true);
    const unsigned irqn = TIMER0_IRQ_0 + alarm;
    scb_hw->scr &= ~M33_SCR_SLEEPDEEP_BITS;  // plain sleep: WFI must not request deep sleep
    clock_crumb(28);
    watchdog_enable(1000, true);  // ticks from clk_ref (the crystal), so it keeps counting while we sleep
    hw_set_bits(&timer_hw->inte, 1u << alarm);
    irq_set_enabled(irqn, true);
    while (gpio_get(pin)) {  // released = high (pull-up); pressed = low
        watchdog_update();
        timer_hw->alarm[alarm] = timer_hw->timerawl + 20000u;  // 20 ms
        __wfi();
        timer_hw->intr = 1u << alarm;
        irq_clear(irqn);
    }
    irq_set_enabled(irqn, false);
    hw_clear_bits(&timer_hw->inte, 1u << alarm);
    timer_hw->intr = 1u << alarm;
    irq_clear(irqn);
    hardware_alarm_unclaim(alarm);
}
#endif
}  // namespace

bool board_sleep_active() { return g_asleep; }

// Core 1 while asleep: deep sleep in WFE (needed for the clock gating: both cores must be in deep sleep), until core 0
// clears the flag and signals with __sev().
void board_sleep_core1_park() {
    scb_hw->scr |= M33_SCR_SLEEPDEEP_BITS;
    while (g_asleep) __wfe();
    scb_hw->scr &= ~M33_SCR_SLEEPDEEP_BITS;
}

void board_sleep_info(char *buf, unsigned n) {
    snprintf(buf, n, "sleeps %lu last wake cause %lx (1 = B pressed)", (unsigned long)g_count, (unsigned long)g_cause);
}

#if SLEEP_SCREEN_ONLY
// Screen and sound off, nothing else changes; USB stays up, so the log keeps running while asleep: every 2 s one
// "SLP alive" line (every 10 s) with the time asleep and the chip's own temperature (ADC4).
static void wake_irq(uint, uint32_t) {}  // only there to end WFI
static bool beat_cb(repeating_timer_t *) { return true; }

static float die_temp_c() { return core_temp_read_now(); }  // board/core_temp.cpp (regulator in PWM mode for the read)
float board_die_temp_c() { return die_temp_c(); }

// RP2350-E9 (datasheet App. D.5.1, chip A2): a GPIO with its input buffer enabled whose pad sits between VIL and VIH
// leaks ~120 uA and holds itself near 2.2 V; only disabling the input (IE) removes it. While asleep nothing but B
// (the wake pin) is read, so every other bank-0 pad gets IE off; the buttons also lose their pull-ups (GP11 reads
// pressed permanently, i.e. it is tied low: its pull-up alone costs ~60 uA). Pads are restored exactly at the wake.
namespace {
uint32_t g_pads[NUM_BANK0_GPIOS];
unsigned g_padsChanged = 0;
void pads_sleep(unsigned wakePin) {
    g_padsChanged = 0;
    for (unsigned pin = 0; pin < NUM_BANK0_GPIOS; ++pin) {
        g_pads[pin] = pads_bank0_hw->io[pin];
        if (pin == wakePin) continue;  // B: input + pull-up, the wake source
#ifdef PICO_SMPS_MODE_PIN
        if (pin == PICO_SMPS_MODE_PIN) continue;  // driven output (regulator mode), left alone
#endif
        uint32_t clr = PADS_BANK0_GPIO0_IE_BITS;
        if (pin == BTN_PIN_X || pin == BTN_PIN_A || pin == BTN_PIN_B || pin == BTN_PIN_Y)
            clr |= PADS_BANK0_GPIO0_PUE_BITS | PADS_BANK0_GPIO0_PDE_BITS;
        if (g_pads[pin] & clr) ++g_padsChanged;
        hw_clear_bits(&pads_bank0_hw->io[pin], clr);
    }
}
void pads_wake() {
    for (unsigned pin = 0; pin < NUM_BANK0_GPIOS; ++pin) pads_bank0_hw->io[pin] = g_pads[pin];
}

void sleep_gate_masks(bool usb, uint32_t &en0, uint32_t &en1) {
#if SLEEP_GATE_LEVEL >= 1
    en0 = CLOCKS_SLEEP_EN0_CLK_SYS_IO_BITS | CLOCKS_SLEEP_EN0_CLK_SYS_PADS_BITS | CLOCKS_SLEEP_EN0_CLK_SYS_PSM_BITS |
          CLOCKS_SLEEP_EN0_CLK_SYS_RESETS_BITS | CLOCKS_SLEEP_EN0_CLK_SYS_CLOCKS_BITS |
          CLOCKS_SLEEP_EN0_CLK_SYS_POWMAN_BITS | CLOCKS_SLEEP_EN0_CLK_REF_POWMAN_BITS |
          CLOCKS_SLEEP_EN0_CLK_SYS_PLL_SYS_BITS | CLOCKS_SLEEP_EN0_CLK_SYS_PLL_USB_BITS;
    en1 = CLOCKS_SLEEP_EN1_CLK_SYS_TIMER0_BITS | CLOCKS_SLEEP_EN1_CLK_SYS_TICKS_BITS |
          CLOCKS_SLEEP_EN1_CLK_REF_TICKS_BITS | CLOCKS_SLEEP_EN1_CLK_SYS_WATCHDOG_BITS |
          CLOCKS_SLEEP_EN1_CLK_SYS_XOSC_BITS;
    if (usb) en1 |= CLOCKS_SLEEP_EN1_CLK_USB_BITS | CLOCKS_SLEEP_EN1_CLK_SYS_USBCTRL_BITS;
#if SLEEP_GATE_LEVEL <= 2
    en0 |= CLOCKS_SLEEP_EN0_CLK_SYS_BUSFABRIC_BITS;
    en1 |= CLOCKS_SLEEP_EN1_CLK_SYS_SRAM0_BITS | CLOCKS_SLEEP_EN1_CLK_SYS_SRAM1_BITS |
           CLOCKS_SLEEP_EN1_CLK_SYS_SRAM2_BITS | CLOCKS_SLEEP_EN1_CLK_SYS_SRAM3_BITS |
           CLOCKS_SLEEP_EN1_CLK_SYS_SRAM4_BITS | CLOCKS_SLEEP_EN1_CLK_SYS_SRAM5_BITS |
           CLOCKS_SLEEP_EN1_CLK_SYS_SRAM6_BITS | CLOCKS_SLEEP_EN1_CLK_SYS_SRAM7_BITS |
           CLOCKS_SLEEP_EN1_CLK_SYS_SRAM8_BITS | CLOCKS_SLEEP_EN1_CLK_SYS_SRAM9_BITS;
#endif
#if SLEEP_GATE_LEVEL <= 1
    en0 |= CLOCKS_SLEEP_EN0_CLK_SYS_ROM_BITS | CLOCKS_SLEEP_EN0_CLK_SYS_SIO_BITS;
    en1 |= CLOCKS_SLEEP_EN1_CLK_SYS_XIP_BITS;
#endif
#else
    (void)usb;  // Round 9e: everything on except the unused peripherals
    en0 = clocks_hw->sleep_en0 & ~(CLOCKS_SLEEP_EN0_CLK_SYS_SHA256_BITS | CLOCKS_SLEEP_EN0_CLK_SYS_PWM_BITS |
                                   CLOCKS_SLEEP_EN0_CLK_SYS_PIO2_BITS | CLOCKS_SLEEP_EN0_CLK_SYS_PIO1_BITS |
                                   CLOCKS_SLEEP_EN0_CLK_SYS_PIO0_BITS | CLOCKS_SLEEP_EN0_CLK_SYS_JTAG_BITS |
                                   CLOCKS_SLEEP_EN0_CLK_SYS_I2C1_BITS | CLOCKS_SLEEP_EN0_CLK_SYS_I2C0_BITS |
                                   CLOCKS_SLEEP_EN0_CLK_SYS_HSTX_BITS | CLOCKS_SLEEP_EN0_CLK_HSTX_BITS |
                                   CLOCKS_SLEEP_EN0_CLK_SYS_DMA_BITS | CLOCKS_SLEEP_EN0_CLK_SYS_ADC_BITS |
                                   CLOCKS_SLEEP_EN0_CLK_ADC_BITS);
    en1 = clocks_hw->sleep_en1 & ~(CLOCKS_SLEEP_EN1_CLK_SYS_UART1_BITS | CLOCKS_SLEEP_EN1_CLK_PERI_UART1_BITS |
                                   CLOCKS_SLEEP_EN1_CLK_SYS_UART0_BITS | CLOCKS_SLEEP_EN1_CLK_PERI_UART0_BITS |
                                   CLOCKS_SLEEP_EN1_CLK_SYS_TRNG_BITS | CLOCKS_SLEEP_EN1_CLK_SYS_TIMER1_BITS |
                                   CLOCKS_SLEEP_EN1_CLK_SYS_TBMAN_BITS | CLOCKS_SLEEP_EN1_CLK_SYS_SPI1_BITS |
                                   CLOCKS_SLEEP_EN1_CLK_PERI_SPI1_BITS | CLOCKS_SLEEP_EN1_CLK_SYS_SPI0_BITS |
                                   CLOCKS_SLEEP_EN1_CLK_PERI_SPI0_BITS);
#endif
}
}  // namespace


void board_sleep_until_pressed(unsigned wakePin) {
    clock_crumb(20);
    lcd_sleep();      // also waits for the display DMA: SLEEP needs no outstanding DMA transfer (§6.5.2)
    sound_suspend();  // speaker pin driven low, input off (RP2350-E9)
    clock_crumb(21);
    // USB clocks stay on while asleep, cable or not (Round 10 tests 1+2: both sleeps that gated them with the cable in
    // never woke; GPIO24 is no usable VBUS sense on this board, it read 0 on USB even with IE on). Cost: USBCTRL
    // ~1.25 mA DVDD (datasheet Table 1691), too small to warm the board.
    const bool usb = true;
    const uint32_t t0 = time_us_32();
    uint32_t nextLog = 0;
    stdio_flush();
    clock_crumb(40);
    const bool slow = clock_sleep_slow(SLEEP_SYS_KHZ);
    clock_crumb(41);
    const float tEnter = die_temp_c();  // before the pads change (the read needs GPIO23, which stays driven anyway)
    pads_sleep(wakePin);
    uint32_t gate0, gate1;
    sleep_gate_masks(usb, gate0, gate1);
    printf("SLP alive 0 s temp %.1f C (screen-only sleep, cores in WFI/WFE, sys %lu kHz, vreg %d = 1.%02d V, chip A%u, "
           "pads IE-off %u, gate level %d en %08lx %08lx usb %d)\n",
           (double)tEnter, (unsigned long)(clock_get_hz(clk_sys) / 1000u), (int)vreg_get_voltage(),
           (int)(vreg_get_voltage() - VREG_VOLTAGE_1_00) * 5, (unsigned)rp2350_chip_version(), g_padsChanged,
           SLEEP_GATE_LEVEL, (unsigned long)gate0, (unsigned long)gate1, (int)usb);
    g_asleep = true;  // core 1 parks in WFE (core1_main)
    __sev();
    // Wake sources for core 0: B going low (GPIO interrupt) and a 1 s alarm for the watchdog and the log line.
    gpio_set_irq_enabled_with_callback(wakePin, GPIO_IRQ_EDGE_FALL, true, &wake_irq);
    repeating_timer_t beat;
    add_repeating_timer_ms(-1000, &beat_cb, nullptr, &beat);
    // Clock gating while both cores are in deep sleep (README Round 9e, Round 10: datasheet §6.5.2 minimum)
    const uint32_t en0 = clocks_hw->sleep_en0, en1 = clocks_hw->sleep_en1;
    clocks_hw->sleep_en0 = gate0;
    clocks_hw->sleep_en1 = gate1;
    scb_hw->scr |= M33_SCR_SLEEPDEEP_BITS;  // this core's WFI is a deep sleep now
    while (gpio_get(wakePin)) {  // released = high
        clock_guard_feed();
        const uint32_t s = (time_us_32() - t0) / 1000000u;
#if GAME_TEMP_TEST
        if (s >= 90) break;  // test build: wake by itself after 90 s
        if (s >= nextLog + 2) {
            nextLog = s;
            const float c = die_temp_c();
            printf("TMP %lu ms phase sleep temp %.1f\n", (unsigned long)(time_us_32() / 1000u), (double)c);
            core_temp_ring_add(3, c);
        }
        core_temp_ring_poll();
#else
        if (s >= nextLog + 2) {  // every 2 s (Round 10 diagnosis), temperature every 10 s
            nextLog = s;
            if (s % 10 < 2) printf("SLP alive %lu s temp %.1f C\n", (unsigned long)s, (double)die_temp_c());
            else printf("SLP alive %lu s\n", (unsigned long)s);
        }
#endif
        clock_crumb(44);
        __wfi();  // until the next interrupt (B, the 1 s alarm, or USB when the cable is in)
        clock_crumb(45);
    }
    scb_hw->scr &= ~M33_SCR_SLEEPDEEP_BITS;
    clocks_hw->sleep_en0 = en0;
    clocks_hw->sleep_en1 = en1;
    pads_wake();
    clock_crumb(46);
    printf("SLP wake 1 (B seen, gating + pads restored)\n");
    stdio_flush();
    cancel_repeating_timer(&beat);
    gpio_set_irq_enabled(wakePin, GPIO_IRQ_EDGE_FALL, false);
    clock_crumb(42);
    if (slow) clock_sleep_restore();  // full speed again before the screen and the speaker come back
    clock_crumb(43);
    printf("SLP wake 2 (clock %lu kHz)\n", (unsigned long)(clock_get_hz(clk_sys) / 1000u));
    stdio_flush();
    g_asleep = false;
    __sev();  // release core 1
    g_ms = (time_us_32() - t0) / 1000u;
    g_cause = 1;
    ++g_count;
    printf("SLP woke after %lu ms temp %.1f C\n", (unsigned long)g_ms, (double)die_temp_c());
    clock_crumb(24);
    sound_resume();
    clock_crumb(25);
    lcd_wake();
    clock_crumb(26);
    printf("SLP wake 3 (sound + LCD back)\n");
}
#else
void board_sleep_until_pressed(unsigned wakePin) {
    clock_crumb(20);
    if (!powman_timer_is_running()) {
        powman_timer_set_1khz_tick_source_lposc();
        powman_timer_set_ms(0);
        powman_timer_start();
    }
    stdio_flush();

    // 1. the parts around the chip
    lcd_sleep();
    sound_suspend();
    clock_guard_suspend();
    clock_crumb(21);

    // 2. wake source: the (pulled-up, released) button going low. A level, not an edge: edge detection needs a clock and
    //    every clock is stopped while dormant; the button is high now, so this cannot fire early
#if SLEEP_DORMANT
    gpio_set_dormant_irq_enabled(wakePin, GPIO_IRQ_LEVEL_LOW, true);
#endif

    // 3. nothing may run at the wrong clock: interrupts off, system and peripheral clocks onto the crystal, PLLs and
    //    the ring oscillator off, lowest safe core voltage
    const uint32_t irq = save_and_disable_interrupts();
    clock_configure_undivided(clk_ref, CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC, 0, XOSC_HZ);
    clock_configure_undivided(clk_sys, CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLK_REF, 0, XOSC_HZ);
    clock_stop(clk_adc);
    clock_stop(clk_usb);
    clock_configure_undivided(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, XOSC_HZ);
    pll_deinit(pll_sys);
    pll_deinit(pll_usb);
    rosc_disable();
    // 1.10 V (the default) is as low as the brown-out detector allows: 0.90 V resets the chip
    vreg_set_voltage(VREG_VOLTAGE_1_10);

    const uint64_t t0 = powman_timer_get_ms();
#if SLEEP_DORMANT
    xosc_dormant();  // ---- asleep here: the next instruction runs after the button press ----
#else
    light_sleep_until_low(wakePin);  // ---- asleep here (crystal on, cores in WFI, watchdog guarding) ----
#endif
    // First thing after the wake: a visible sign (backlight on) and a watchdog, so a hang in the restore code below ends in a
    // reset whose breadcrumb says where, instead of a console that looks asleep for ever.
    // The SDK's clock setup (runtime_init_clocks) steps through the ring oscillator: it must be running. rosc_restart() would
    // overwrite the whole control register (and its frequency range) with 'enable', so only the enable field is touched.
    {
        uint32_t c = rosc_hw->ctrl;
        c = (c & ~ROSC_CTRL_ENABLE_BITS) | (ROSC_CTRL_ENABLE_VALUE_ENABLE << ROSC_CTRL_ENABLE_LSB);
        rosc_write(&rosc_hw->ctrl, c);
        while (!(rosc_hw->status & ROSC_STATUS_STABLE_BITS)) {
        }
    }
    gpio_put(LCD_PIN_LED, 1);
    clock_crumb(22);
    watchdog_enable(2500, true);
    const uint64_t t1 = powman_timer_get_ms();
    g_ms = (uint32_t)(t1 - t0);
#if SLEEP_DORMANT
    g_cause = io_bank0_hw->dormant_wake_irq_ctrl.ints[wakePin / 8] >> ((wakePin % 8) * 4) & 0xFu;
#else
    g_cause = 1;  // the light sleep only ends on B (or the watchdog resets the board)
#endif
    g_pinAtWake = gpio_get(wakePin);
    ++g_count;

#if SLEEP_DORMANT
    gpio_set_dormant_irq_enabled(wakePin, GPIO_IRQ_LEVEL_LOW, false);
#endif
    clock_crumb(23);
    // Interrupts back on before the clock restore: it uses sleep_ms(), which waits for a timer interrupt
    restore_interrupts(irq);
    clock_reinit_after_sleep();
    clock_crumb(24);

    // 4. the parts around the chip again
    sound_resume();
    clock_crumb(25);
    lcd_wake();
    clock_crumb(26);
    clock_guard_rearm();
    clock_crumb(27);
}
#endif  // SLEEP_SCREEN_ONLY
