// HardFault recorder: the default handler spins until the watchdog resets the board and the cause is
// lost. This one saves the stacked PC/LR and the fault status registers in .noinit RAM (it survives a
// watchdog reset); the next boot prints it in the CFG log line. It then waits for the watchdog as before.
#include <stdint.h>

#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "hardware/structs/sio.h"
#include "board/fault.h"

namespace {
constexpr uint32_t MAGIC = 0xFA17D00Du;
struct Rec {
    uint32_t magic, pc, lr, sp, cfsr, hfsr, bfar, mmfar, core, xpsr;
};
Rec g_rec __attribute__((section(".noinit")));
Rec g_prev;
}  // namespace

extern "C" void fault_c(uint32_t *sp) {
    g_rec.pc = sp[6];
    g_rec.lr = sp[5];
    g_rec.xpsr = sp[7];
    g_rec.sp = (uint32_t)sp;
    g_rec.cfsr = *(volatile uint32_t *)0xE000ED28;
    g_rec.hfsr = *(volatile uint32_t *)0xE000ED2C;
    g_rec.mmfar = *(volatile uint32_t *)0xE000ED34;
    g_rec.bfar = *(volatile uint32_t *)0xE000ED38;
    g_rec.core = sio_hw->cpuid;
    g_rec.magic = MAGIC;
    for (;;) __asm volatile("nop");
}

extern "C" __attribute__((naked)) void isr_hardfault(void) {
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "b fault_c\n");
}

void fault_boot_capture() {
    g_prev = g_rec;
    g_rec.magic = 0;
}

int fault_snapshot(char *buf, int n) {
    if (g_prev.magic != MAGIC) { buf[0] = 0; return 0; }
    return __builtin_snprintf(buf, n, "FAULT core %lu pc %08lx lr %08lx sp %08lx xpsr %08lx cfsr %08lx hfsr %08lx bfar %08lx mmfar %08lx",
                              (unsigned long)g_prev.core, (unsigned long)g_prev.pc, (unsigned long)g_prev.lr,
                              (unsigned long)g_prev.sp, (unsigned long)g_prev.xpsr, (unsigned long)g_prev.cfsr,
                              (unsigned long)g_prev.hfsr, (unsigned long)g_prev.bfar, (unsigned long)g_prev.mmfar);
}
