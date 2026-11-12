#include "storage/high_score.h"

#include <cstdio>
#include <cstring>

#include "devoursphere/sim/high_score_record.hpp"
#include "hardware/flash.h"
#include "hardware/structs/qmi.h"
#include "hardware/regs/addressmap.h"
#include "pico/flash.h"

namespace sim = devoursphere::sim;

namespace {

// Last sector of flash, far away from the firmware image.
constexpr uint32_t RECORD_OFFSET = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;

struct WriteJob {
    uint8_t page[FLASH_PAGE_SIZE];
};

uint32_t g_qmiBefore = 0, g_qmiAfterRom = 0;

// Runs from RAM (not_in_flash): after each ROM flash call the QSPI divider is back at the ROM's default, too
// fast for a 400 MHz core, so no instruction may be fetched from flash until the timing is restored below.
void __not_in_flash_func(write_job)(void *param) {
    const WriteJob *job = static_cast<const WriteJob *>(param);
    // The boot ROM's flash routines put the QSPI timing back to their own default
    // (divider sized for 150 MHz). On an overclocked core that makes the flash
    // clock too fast, so the clock-matched value is restored afterwards.
    g_qmiBefore = qmi_hw->m[0].timing;
    flash_range_erase(RECORD_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(RECORD_OFFSET, job->page, FLASH_PAGE_SIZE);
    g_qmiAfterRom = qmi_hw->m[0].timing;
    qmi_hw->m[0].timing = g_qmiBefore;
    __asm volatile("dsb\n isb" ::: "memory");
}

}  // namespace

bool high_scores_load(HighScores &hs) {
    bool any = false;
    for (int i = 0; i < HS_WEAPONS; ++i) {
        const uint8_t *rec = reinterpret_cast<const uint8_t *>(XIP_BASE + RECORD_OFFSET) + 16 * i;
        uint32_t sc = 0;
        int sp = 0;
        if (sim::decodeHighScoreRecord(rec, &sc, &sp)) {
            hs.score[i] = sc;
            hs.sphere[i] = sp;
            any = true;
        } else {
            hs.score[i] = 0;
            hs.sphere[i] = 0;
        }
    }
    printf("HS load: %lu / %lu / %lu\n", (unsigned long)hs.score[0], (unsigned long)hs.score[1], (unsigned long)hs.score[2]);
    return any;
}

bool high_scores_save(const HighScores &hs) {
    static WriteJob job;
    memset(job.page, 0xFF, sizeof(job.page));
    for (int i = 0; i < HS_WEAPONS; ++i) {
        if (hs.score[i] > 0) sim::encodeHighScoreRecord(job.page + 16 * i, hs.score[i], hs.sphere[i]);
    }
    const int rc = flash_safe_execute(write_job, &job, 500);
    if (rc != PICO_OK) {
        printf("HS save: flash_safe_execute failed (%d)\n", rc);
        return false;
    }
    bool ok = true;
    for (int i = 0; i < HS_WEAPONS; ++i) {
        uint32_t chk = 0;
        int chkSphere = 0;
        const bool valid = sim::decodeHighScoreRecord(reinterpret_cast<const uint8_t *>(XIP_BASE + RECORD_OFFSET) + 16 * i, &chk, &chkSphere);
        if (hs.score[i] > 0 ? !(valid && chk == hs.score[i]) : valid) ok = false;
    }
    printf("HS save: %lu / %lu / %lu %s (qmi %08lx -> rom %08lx -> %08lx)\n", (unsigned long)hs.score[0],
           (unsigned long)hs.score[1], (unsigned long)hs.score[2], ok ? "ok" : "VERIFY FAILED", (unsigned long)g_qmiBefore,
           (unsigned long)g_qmiAfterRom, (unsigned long)qmi_hw->m[0].timing);
    return ok;
}
