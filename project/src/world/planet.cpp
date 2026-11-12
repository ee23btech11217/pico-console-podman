#include "world/planet.h"

#include "pico/stdlib.h"
#include "world/planet_core.h"

namespace {

namespace g2 = shapoco::gfx2d;

uint32_t g_paintUs = 0;
uint32_t g_genUs = 0;

// Background generation hand-off between the two cores (serial numbers, one writer each)
volatile uint32_t g_reqSeed = 0;
volatile int g_reqStyle = -1;
volatile uint32_t g_reqSerial = 0;   // core0: bumped per request
uint32_t g_workSerial = 0;           // the other core: the request it is working on
bool g_working = false;
volatile uint32_t g_doneSerial = 0;  // the other core: last finished request
uint32_t g_commitSerial = 0;         // core0: last request swapped in
uint32_t g_reqUs = 0;

#if PLANET_SHARE
// Dynamic work sharing of the planet surface (README Round 11). Each band's rows are cut into chunks of
// PLANET_SHARE_ROWS; core 0 (in the band hook) and core 1 (in its idle loop, when no sim batch runs) claim chunks
// from g_claim = (job serial << 8) | next chunk with a compare-and-swap, so a core can only claim chunks of the job
// that is current; core 0 starts no new job before every claimed chunk is done (g_doneChunks), so the job data is
// stable while anyone works on it. Core 0 never waits for core 1 to start: if core 1 is busy, core 0 takes all.
#ifndef PLANET_SHARE_ROWS
#define PLANET_SHARE_ROWS 4
#endif
static_assert(PLANET_SHARE_ROWS % 2 == 0, "chunks must be whole 2-row planet blocks");
static_assert(LCD_BAND_H_CFG / PLANET_SHARE_ROWS < 255, "chunk index 255 marks a closed job");
struct ShareJob {
    planet::View v;
    uint16_t *dst;
    int pitch, y, h, width, chunks;
};
ShareJob g_job;
uint32_t g_claim = 0;       // (serial << 8) | next chunk index
uint32_t g_doneChunks = 0;  // chunks finished in the current job
uint32_t g_serial = 0;      // core 0 only
uint32_t g_helpChunks = 0, g_allChunks = 0;  // statistics: chunks painted by core 1 / all

// Claim one chunk of the current job (any core). Returns the chunk index or -1.
int __not_in_flash_func(share_claim)(uint32_t serial) {
    uint32_t w = __atomic_load_n(&g_claim, __ATOMIC_ACQUIRE);
    for (;;) {
        if ((w >> 8) != (serial & 0xFFFFFFu) || (int)(w & 0xFFu) >= g_job.chunks) return -1;
        if (__atomic_compare_exchange_n(&g_claim, &w, w + 1, true, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return (int)(w & 0xFFu);
    }
}

void __not_in_flash_func(share_paint)(int i) {
    const int r0 = i * PLANET_SHARE_ROWS;
    const int rows = g_job.h - r0 < PLANET_SHARE_ROWS ? g_job.h - r0 : PLANET_SHARE_ROWS;
    planet::render(g_job.v, g_job.dst + (size_t)r0 * g_job.pitch, g_job.pitch, g_job.y + r0, rows, g_job.width);
    __atomic_fetch_add(&g_doneChunks, 1u, __ATOMIC_RELEASE);
}
#endif

void __not_in_flash_func(band_hook)(const devoursphere::render::Renderer::BackdropView &bv, const g2::Surface &dst, int y, int h,
               int dstY) {
    const uint32_t t0 = time_us_32();
    planet::View v;
    for (int i = 0; i < 3; ++i) {
        v.eye[i] = bv.eye[i];
        v.right[i] = bv.right[i];
        v.up[i] = bv.up[i];
        v.fwd[i] = bv.fwd[i];
    }
    v.focal = bv.focal;
    v.cx = bv.cx;
    v.cy = bv.cy;
    v.radius16 = bv.sphereRadius16;
#if PLANET_SHARE
    g_job.v = v;
    g_job.dst = (uint16_t *)dst.linePtr(dstY);
    g_job.pitch = (int)(dst.stride / 2);
    g_job.y = y;
    g_job.h = h;
    g_job.width = dst.width;
    g_job.chunks = (h + PLANET_SHARE_ROWS - 1) / PLANET_SHARE_ROWS;
    __atomic_store_n(&g_doneChunks, 0u, __ATOMIC_RELAXED);
    const uint32_t serial = ++g_serial & 0xFFFFFFu;
    __atomic_store_n(&g_claim, serial << 8, __ATOMIC_RELEASE);  // publishes the job
    uint32_t mine = 0;
    for (int i; (i = share_claim(serial)) >= 0; ++mine) share_paint(i);
    while (__atomic_load_n(&g_doneChunks, __ATOMIC_ACQUIRE) < (uint32_t)g_job.chunks) {
        // only a chunk core 1 has already claimed is left (a few hundred us at most)
    }
    // Close the job (index 255 >= any chunk count) before g_job is rewritten for the next band: a core that read the
    // old claim word can then never claim a chunk of a half-written job.
    __atomic_store_n(&g_claim, (serial << 8) | 0xFFu, __ATOMIC_RELEASE);
    g_allChunks += (uint32_t)g_job.chunks;
    g_helpChunks += (uint32_t)g_job.chunks - mine;
#else
    planet::render(v, (uint16_t *)dst.linePtr(dstY), (int)(dst.stride / 2), y, h, dst.width);
#endif
    g_paintUs += time_us_32() - t0;
}

// The player's body takes its pattern from the current world: PLANET_SKIN_MIX percent world colour,
// the rest the player's own fragment colour, so the player stays recognisable on every world.
#ifndef PLANET_SKIN_MIX
#define PLANET_SKIN_MIX 62
#endif

bool skin_hook(bool isPlayer, g2::Color base, int32_t lx, int32_t ly, int32_t bodyR, g2::Color &out) {
    if (!isPlayer) return false;
    uint8_t w[3];
    planet::skin(lx, ly, bodyR, w);
    const int mix = PLANET_SKIN_MIX;
    int r = (w[0] * mix + g2::colorR(base) * (100 - mix)) / 100;
    int g = (w[1] * mix + g2::colorG(base) * (100 - mix)) / 100;
    int b = (w[2] * mix + g2::colorB(base) * (100 - mix)) / 100;
    r += r >> 3;  // a little brighter than the ground it flies over
    g += g >> 3;
    b += b >> 3;
    out = g2::makeColor(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b);
    return true;
}

#ifndef BODY_LIGHT
#define BODY_LIGHT 1
#endif
#ifndef BODY_LIGHT_AMB
#define BODY_LIGHT_AMB 45  // percent of the body colour in shadow
#endif
#ifndef BODY_LIGHT_SUN
#define BODY_LIGHT_SUN 80  // percent added in full sun (sum above 100 is clamped per channel)
#endif

// Round 17: the bodies are lit by the same sun as the ground (fork patch 10)
bool body_light_hook(float toSun[3]) {
    planet::sunDir(toSun);
    return true;
}

}  // namespace

void planet_install(devoursphere::render::Renderer &r) {
    r.setBandHook(band_hook);
    r.setSkinHook(skin_hook);
#if BODY_LIGHT
    r.setBodyLightHook(body_light_hook, BODY_LIGHT_AMB / 100.0f, BODY_LIGHT_SUN / 100.0f);
#endif
}

void planet_set_world(uint32_t seed, int style) {
    const uint32_t t0 = time_us_32();
    planet::generate(seed, style);
    g_genUs = time_us_32() - t0;
}

void planet_request_world(uint32_t seed, int style) {
    g_reqSeed = seed;
    g_reqStyle = style;
    g_reqUs = time_us_32();
    __atomic_store_n(&g_reqSerial, g_reqSerial + 1, __ATOMIC_RELEASE);
}

bool __not_in_flash_func(planet_help_poll)() {
#if PLANET_SHARE
    const uint32_t w = __atomic_load_n(&g_claim, __ATOMIC_ACQUIRE);
    const int i = share_claim(w >> 8);
    if (i < 0) return false;
    share_paint(i);
    return true;
#else
    return false;
#endif
}

bool planet_gen_poll() {
    const uint32_t serial = __atomic_load_n(&g_reqSerial, __ATOMIC_ACQUIRE);
    if (serial != g_workSerial) {  // a new request (also restarts one in progress)
        planet::gen_begin(g_reqSeed, g_reqStyle);
        g_workSerial = serial;
        g_working = true;
    }
    if (!g_working) return false;
    if (planet::gen_step()) {
        g_working = false;
        __atomic_store_n(&g_doneSerial, g_workSerial, __ATOMIC_RELEASE);
    }
    return true;
}

#if PLANET_ANIM
volatile uint32_t g_animUs = 0;
bool (*g_animSimWaiting)() = nullptr;

// The animation gives way between its units of work: to a sim batch core 0 has asked for, and to an open planet
// chunk job (core 1 helps painting first).
bool __not_in_flash_func(anim_yield)() {
    if (g_animSimWaiting && g_animSimWaiting()) return true;
#if PLANET_SHARE
    const uint32_t w = __atomic_load_n(&g_claim, __ATOMIC_ACQUIRE);
    if ((int)(w & 0xFFu) < g_job.chunks) return true;
#endif
    return false;
}
#endif

bool __not_in_flash_func(planet_anim_poll)(bool (*simWaiting)()) {
#if PLANET_ANIM
    g_animSimWaiting = simWaiting;
    if (anim_yield()) return false;  // not even one unit while someone needs this core or the memory
    const uint32_t t0 = time_us_32();
    if (!planet::anim_step(t0 / 1000u, anim_yield)) return false;
    g_animUs += time_us_32() - t0;  // written by core 1 only; core 0 reads and clears it once a second
    return true;
#else
    (void)simWaiting;
    return false;
#endif
}

void planet_commit_if_ready() {
    const uint32_t done = __atomic_load_n(&g_doneSerial, __ATOMIC_ACQUIRE);
    if (done != g_commitSerial && done == g_reqSerial) {
        planet::gen_commit();
        g_commitSerial = done;
        g_genUs = time_us_32() - g_reqUs;
    }
}

PlanetStats planet_take_stats() {
    PlanetStats s;
    s.paint_us = g_paintUs;
    s.blocks = planet::takeBlocks();
    s.gen_us = g_genUs;
    s.palette = planet::paletteIndex();
#if PLANET_SHARE
    s.help_permille = g_allChunks ? g_helpChunks * 1000u / g_allChunks : 0;
    g_helpChunks = g_allChunks = 0;
#else
    s.help_permille = 0;
#endif
#if PLANET_ANIM
    s.anim_us = g_animUs;
    g_animUs = 0;
#else
    s.anim_us = 0;
#endif
    g_paintUs = 0;
    g_genUs = 0;
    return s;
}
