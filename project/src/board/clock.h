#pragma once

#include <cstdint>

// Raises the system clock (0 = leave the stock 150 MHz) and, when the flash is
// executed in place, slows the QSPI divider so the flash never exceeds ~100 MHz.
void clock_init(uint32_t sys_khz);

// Watchdog guard for overclocked builds. clock_guard_begin() must run before
// clock_init(): it reports whether the previous boot was killed by the watchdog
// while still "unproven" (-> the caller must fall back to the stock clock), then
// arms the watchdog. clock_guard_feed() is called once per completed frame;
// after ~5 s of frames the boot is marked proven.
bool clock_guard_begin();
void clock_guard_feed();

// Boot facts for the log line.
uint32_t clock_qmi_timing();
uint32_t clock_wdt_reason();
uint32_t clock_boot_scratch0();
uint32_t clock_boot_scratch(int i);

// Boot breadcrumbs: clock_crumb(n) records the last start-up step reached in a watchdog scratch register (it survives
// a watchdog reset); clock_prev_crumb() is the value the previous boot left, for the log.
void clock_crumb(uint32_t n);

// Sleep support: the watchdog must not run while the chip is dormant, and the system clock set up by clock_init() has to
// be rebuilt after a wake.
void clock_guard_suspend();
void clock_guard_rearm();
void clock_reinit_after_sleep();
// Screen-only sleep: system clock down through the PLL (false = not reachable, nothing changed) and back.
bool clock_sleep_slow(uint32_t khz);
void clock_sleep_restore();
uint32_t clock_prev_crumb();
