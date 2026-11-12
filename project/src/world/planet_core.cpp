#include "world/planet_core.h"

#include <cmath>
#include <cstring>

#ifdef PLANET_HOST
#define PLANET_HOT
#else
#include "pico.h"
#define PLANET_HOT __attribute__((section(".time_critical.planet")))
#endif

#ifndef PLANET_BLOCK_W
#define PLANET_BLOCK_W 2
#endif
// A block farther than this (FU) is shaded once for 2 * PLANET_BLOCK_W pixels: its fine layers have
// faded out, so the wider block is not visible and the work per frame drops by about a third.
// Skin of the flying bodies: texels of the terrain tile per body radius, and the palette brightness
// step (24 = nominal, up to 31)
#ifndef PLANET_SKIN_TEXELS
#define PLANET_SKIN_TEXELS 18.0f
#endif
#ifndef PLANET_SKIN_SHADE
#define PLANET_SKIN_SHADE 28
#endif
#ifndef PLANET_WIDE_FU
#define PLANET_WIDE_FU 30.0f
#endif
#ifndef PLANET_BLOCK_H
#define PLANET_BLOCK_H 2
#endif
// Distance fade, in fragment units (1 FU = 256 world units = 4096 of the 1/16 units)
#ifndef PLANET_FOG_NEAR_FU
#define PLANET_FOG_NEAR_FU 55.0f
#endif
#ifndef PLANET_FOG_FAR_FU
#define PLANET_FOG_FAR_FU 330.0f
#endif
// Two tiling layers on top of the coarse per-world map: "hills" (mid-scale terrain, also lit as
// relief) and "grain" (fine ground texture). Tile size and the distance (FU) where each has faded out.
#ifndef PLANET_HILL_TILE_FU
#define PLANET_HILL_TILE_FU 28.0f
#endif
#ifndef PLANET_HILL_FADE_FU
#define PLANET_HILL_FADE_FU 170.0f
#endif
#ifndef PLANET_GRAIN_TILE_FU
#define PLANET_GRAIN_TILE_FU 5.0f
#endif
#ifndef PLANET_GRAIN_FADE_FU
#define PLANET_GRAIN_FADE_FU 26.0f
#endif

namespace planet {

namespace {

constexpr int N = FACE_N;
constexpr float FU16 = 4096.0f;  // 1/16 units per FU

// Everything that makes up one world. Two sets: render() paints from the front one while the next
// world is built into the back one (in small slices, possibly on the other core), then they swap.
struct Maps {
    alignas(4) uint8_t cube[6][N * N];
    alignas(4) uint8_t hills[DETAIL_N * DETAIL_N];
    alignas(4) uint8_t grain[DETAIL_N * DETAIL_N];
    int palette;
    float sun[3];
};
Maps g_sets[2];
// [height][shade] -> RGB565 byte-swapped. One table for the current world, rebuilt (about 1 ms) when a new world
// is swapped in, so the sets do not each carry 16 KB.
alignas(4) uint16_t g_lut[256 * SHADES];
// The current world's palette colour per height (8-bit RGB), kept for the animated table (anim_step)
uint8_t g_base[256][3];
volatile int g_front = 0;
uint32_t g_blocks = 0;
float g_wideFu = PLANET_WIDE_FU;

// Progress of the world being built into the back set
struct Gen {
    uint32_t seed;
    int style;
    int step;
    float freq, ox, oy, oz, lo, hi, k;
    bool active, ready;
};
Gen g_gen;

// ---------------------------------------------------------------- noise
inline uint32_t ihash(int32_t x, int32_t y, int32_t z, uint32_t s) {
    uint32_t h = (uint32_t)x * 0x8da6b343u ^ (uint32_t)y * 0xd8163841u ^ (uint32_t)z * 0xcb1ab31fu ^
                 s * 0x9e3779b9u;
    h ^= h >> 13;
    h *= 0x85ebca6bu;
    h ^= h >> 16;
    return h;
}
inline float h01(int32_t x, int32_t y, int32_t z, uint32_t s) { return (float)(ihash(x, y, z, s) >> 8) * (1.0f / 16777216.0f); }
inline float fade(float t) { return t * t * (3.0f - 2.0f * t); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

float vnoise3(float x, float y, float z, uint32_t s) {
    const float fx = floorf(x), fy = floorf(y), fz = floorf(z);
    const int32_t ix = (int32_t)fx, iy = (int32_t)fy, iz = (int32_t)fz;
    const float tx = fade(x - fx), ty = fade(y - fy), tz = fade(z - fz);
    const float c000 = h01(ix, iy, iz, s), c100 = h01(ix + 1, iy, iz, s);
    const float c010 = h01(ix, iy + 1, iz, s), c110 = h01(ix + 1, iy + 1, iz, s);
    const float c001 = h01(ix, iy, iz + 1, s), c101 = h01(ix + 1, iy, iz + 1, s);
    const float c011 = h01(ix, iy + 1, iz + 1, s), c111 = h01(ix + 1, iy + 1, iz + 1, s);
    return lerpf(lerpf(lerpf(c000, c100, tx), lerpf(c010, c110, tx), ty),
                 lerpf(lerpf(c001, c101, tx), lerpf(c011, c111, tx), ty), tz);
}

float fbm3(float x, float y, float z, uint32_t s) {
    float sum = 0, amp = 0.5f, norm = 0;
    for (int o = 0; o < 5; ++o) {
        sum += amp * vnoise3(x, y, z, s + 131u * (uint32_t)o);
        norm += amp;
        amp *= 0.5f;
        x *= 2.03f;
        y *= 2.03f;
        z *= 2.03f;
    }
    return sum / norm;
}

// Tileable 2D value noise: lattice of `period` cells that wraps around.
float vnoise2t(float x, float y, int period, uint32_t s) {
    const float fx = floorf(x), fy = floorf(y);
    const int ix = (int)fx, iy = (int)fy;
    const float tx = fade(x - fx), ty = fade(y - fy);
    const int m = period - 1;
    auto c = [&](int a, int b) { return h01(a & m, b & m, 7, s); };
    return lerpf(lerpf(c(ix, iy), c(ix + 1, iy), tx), lerpf(c(ix, iy + 1), c(ix + 1, iy + 1), tx), ty);
}

// ---------------------------------------------------------------- palettes
struct Stop {
    uint8_t h, r, g, b;
};
struct Palette {
    const char *name;
    const Stop *stops;
    int count;
};

const Stop PAL_TERRA[] = {{0, 6, 22, 70},   {60, 12, 48, 120},  {88, 24, 100, 150}, {96, 150, 150, 110},
                          {104, 196, 176, 112}, {112, 70, 140, 62}, {150, 38, 100, 48}, {185, 34, 80, 44},
                          {205, 110, 98, 84}, {228, 160, 156, 150}, {246, 236, 242, 250}, {255, 255, 255, 255}};
const Stop PAL_MARS[] = {{0, 96, 40, 30},   {70, 160, 74, 40},  {120, 214, 112, 56}, {170, 236, 150, 84},
                         {215, 190, 118, 80}, {240, 244, 206, 176}, {255, 252, 238, 224}};
const Stop PAL_ICE[] = {{0, 64, 104, 176},  {70, 120, 168, 224}, {120, 196, 220, 244}, {180, 228, 242, 253},
                        {230, 246, 252, 255}, {255, 255, 255, 255}};
const Stop PAL_LAVA[] = {{0, 255, 120, 30},  {38, 214, 52, 14},  {75, 80, 20, 14},   {110, 34, 28, 30},
                         {170, 52, 46, 48},  {220, 92, 84, 84},  {255, 128, 120, 118}};
const Stop PAL_ALIEN[] = {{0, 30, 12, 70},   {60, 70, 30, 120},  {100, 30, 150, 150}, {130, 40, 190, 160},
                          {170, 190, 60, 160}, {215, 120, 40, 140}, {255, 240, 214, 255}};

const Palette PALS[PALETTES] = {{"terra", PAL_TERRA, 12}, {"mars", PAL_MARS, 7}, {"ice", PAL_ICE, 6},
                                {"lava", PAL_LAVA, 7},    {"alien", PAL_ALIEN, 7}};

void paletteColor(const Palette &p, int h, float &r, float &g, float &b) {
    int i = 0;
    while (i + 2 < p.count && h > p.stops[i + 1].h) ++i;
    const Stop &a = p.stops[i], &c = p.stops[i + 1];
    const float t = c.h > a.h ? (float)(h - a.h) / (float)(c.h - a.h) : 0.0f;
    const float tt = t < 0 ? 0 : (t > 1 ? 1 : t);
    r = lerpf(a.r, c.r, tt);
    g = lerpf(a.g, c.g, tt);
    b = lerpf(a.b, c.b, tt);
}

inline uint16_t pack565swapped(float r, float g, float b) {
    int R = (int)r, G = (int)g, B = (int)b;
    R = R < 0 ? 0 : (R > 255 ? 255 : R);
    G = G < 0 ? 0 : (G > 255 ? 255 : G);
    B = B < 0 ? 0 : (B > 255 ? 255 : B);
    const uint16_t v = (uint16_t)(((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3));
    return (uint16_t)((v >> 8) | (v << 8));
}

// Direction of cube-map texel (face f, i, j): the face's major axis k = f >> 1, sign from f & 1.
void texelDir(int f, int i, int j, float d[3]) {
    const int k = f >> 1;
    const float sgn = (f & 1) ? -1.0f : 1.0f;
    const float s = ((float)i + 0.5f) / N * 2.0f - 1.0f;
    const float t = ((float)j + 0.5f) / N * 2.0f - 1.0f;
    d[k] = sgn;
    d[(k + 1) % 3] = s;
    d[(k + 2) % 3] = t;
    const float inv = 1.0f / sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    d[0] *= inv;
    d[1] *= inv;
    d[2] *= inv;
}

}  // namespace

int paletteIndex() { return g_sets[g_front].palette; }
const char *paletteName(int i) { return PALS[i % PALETTES].name; }
uint32_t takeBlocks() {
    const uint32_t b = g_blocks;
    g_blocks = 0;
    return b;
}

namespace {
// Number of steps gen_step() takes; each is a few ms at most on a 400 MHz core
constexpr int STEP_SETUP = 0;
constexpr int STEP_RANGE0 = 1;                      // 6 steps: value range of each face (coarse)
constexpr int STEP_RANGE_END = STEP_RANGE0 + 6;     // (the last range step also fixes the stretch factor)
constexpr int STEP_CUBE0 = STEP_RANGE_END;          // 24 steps: 16 rows of one face each
constexpr int STEP_LAYERS0 = STEP_CUBE0 + 24;       // 4 steps: 16 rows of hills + grain each
constexpr int STEP_LAST = STEP_LAYERS0 + 4 - 1;     // the last layers step finishes the world

inline float sampleWorld(const Gen &g, int f, int i, int j) {
    float d[3];
    texelDir(f, i, j, d);
    return fbm3(d[0] * g.freq + g.ox, d[1] * g.freq + g.oy, d[2] * g.freq + g.oz, g.seed);
}
}  // namespace

void gen_begin(uint32_t seed, int style) {
    g_gen = Gen{};
    g_gen.seed = seed;
    g_gen.style = style;
#ifdef PLANET_FORCE_STYLE
    if (style < 0) g_gen.style = PLANET_FORCE_STYLE;  // test builds (GAME_PLANET_DEFS=PLANET_FORCE_STYLE=n): one world style
#endif
    g_gen.lo = 1e9f;
    g_gen.hi = -1e9f;
    g_gen.active = true;
    g_gen.ready = false;
}

bool gen_step() {
    Gen &g = g_gen;
    if (!g.active) return true;
    Maps &B = g_sets[1 - g_front];
    const int st = g.step++;
    if (st == STEP_SETUP) {
        uint32_t sm = g.seed * 2654435761u + 12345u;
        sm ^= sm >> 15;
        B.palette = g.style >= 0 ? g.style % PALETTES : (int)((sm >> 8) % PALETTES);
        // Where the sun stands differs a little per world
        const float a = (float)((sm >> 16) & 255) * (6.2831853f / 256.0f);
        B.sun[0] = 0.55f * cosf(a);
        B.sun[1] = 0.62f;
        B.sun[2] = 0.55f * sinf(a);
        const float il = 1.0f / sqrtf(B.sun[0] * B.sun[0] + B.sun[1] * B.sun[1] + B.sun[2] * B.sun[2]);
        B.sun[0] *= il;
        B.sun[1] *= il;
        B.sun[2] *= il;
        g.ox = (float)((sm >> 3) & 1023) * 0.37f;
        g.oy = (float)((sm >> 13) & 1023) * 0.29f;
        g.oz = (float)((sm >> 7) & 1023) * 0.41f;
        // Lava worlds are rougher, ice worlds smoother
        g.freq = B.palette == 3 ? 3.2f : (B.palette == 2 ? 2.0f : 2.6f);
    } else if (st < STEP_RANGE_END) {
        // The value range of this world from a coarse sample (16 x 16 per face)
        const int f = st - STEP_RANGE0;
        for (int j = 2; j < N; j += 4)
            for (int i = 2; i < N; i += 4) {
                const float v = sampleWorld(g, f, i, j);
                if (v < g.lo) g.lo = v;
                if (v > g.hi) g.hi = v;
            }
        if (st == STEP_RANGE_END - 1) g.k = 255.0f / (g.hi - g.lo + 1e-6f);  // stretch to the full palette
    } else if (st < STEP_LAYERS0) {
        const int q = st - STEP_CUBE0;
        const int f = q >> 2, j0 = (q & 3) * 16;
        for (int j = j0; j < j0 + 16; ++j)
            for (int i = 0; i < N; ++i) {
                int h = (int)((sampleWorld(g, f, i, j) - g.lo) * g.k);
                B.cube[f][j * N + i] = (uint8_t)(h < 0 ? 0 : (h > 255 ? 255 : h));
            }
    } else {
        // Tileable ground layers (wrapping value noise): hills = broad lumps, grain = fine speckle
        const int y0 = (st - STEP_LAYERS0) * 16;
        const uint32_t seed = g.seed;
        for (int y = y0; y < y0 + 16; ++y)
            for (int x = 0; x < DETAIL_N; ++x) {
                const float fx = (float)x / DETAIL_N, fy = (float)y / DETAIL_N;
                const float hv = 0.55f * vnoise2t(fx * 4, fy * 4, 4, seed ^ 0x1111u) +
                                 0.30f * vnoise2t(fx * 8, fy * 8, 8, seed ^ 0x2222u) +
                                 0.15f * vnoise2t(fx * 16, fy * 16, 16, seed ^ 0x3333u);
                const float gv = 0.5f * vnoise2t(fx * 16, fy * 16, 16, seed ^ 0x4444u) +
                                 0.5f * vnoise2t(fx * 32, fy * 32, 32, seed ^ 0x5555u);
                int d = (int)(hv * 255.0f * 1.4f - 0.2f * 255.0f);
                B.hills[y * DETAIL_N + x] = (uint8_t)(d < 0 ? 0 : (d > 255 ? 255 : d));
                d = (int)(gv * 255.0f);
                B.grain[y * DETAIL_N + x] = (uint8_t)(d < 0 ? 0 : (d > 255 ? 255 : d));
            }
        if (st == STEP_LAST) {
            g.active = false;
            g.ready = true;
            return true;
        }
    }
    return false;
}

namespace {
// Palette x brightness table of a world. Shade 24 is the nominal brightness, up to 31 is brighter.
void buildLut(int palette) {
    const Palette &pal = PALS[palette];
    for (int h = 0; h < 256; ++h) {
        float r, gg, b;
        paletteColor(pal, h, r, gg, b);
        g_base[h][0] = (uint8_t)r;
        g_base[h][1] = (uint8_t)gg;
        g_base[h][2] = (uint8_t)b;
        const bool glow = palette == 3 && h < 60;  // lava stays bright in the dark
        for (int sh = 0; sh < SHADES; ++sh) {
            float m = (float)sh * (1.0f / 24.0f);
            if (glow && m < 0.9f) m = 0.9f;
            g_lut[h * SHADES + sh] = pack565swapped(r * m, gg * m, b * m);
        }
    }
}
}  // namespace

namespace {
// ---------------------------------------------------------------- surface animation (README Rounds 12-13)
// Core 1 animates data the painters read anyway, so a moving world costs them the same as a still one. Integer only.
//   LUT    the colour of every height: surf, shimmer, glints, glow, embers          (256 heights x 32 shades)
//   CUBE   the world's low ground (water, lava, pools, dusty basins, deep ice):      (6 x 64 x 64 texels)
//          three crossing waves, shore lines washing up the beach, creeping lava
//   LAYERS the hills and grain tiles near the player: wind in the grass, migrating   (2 x 64 x 64 texels)
//          dunes, drifting dust and snow, flowing lava lumps, breathing alien hills
// The still copies of the cube map and the tiles live in the *back* world set, which is idle between world changes.
// While a new world is built into it (gen active, or finished and waiting for gen_commit) CUBE and LAYERS pause and
// the world on screen keeps its last frame. After each commit (g_commits changes) the new world is first copied to
// the back set. The only world built outside core 1 is the very first one (planet_set_world on core 0, before
// g_commits becomes 1), and nothing spatial animates before it. Single-core builds never call anim_step().
#ifndef PLANET_ANIM_PERIOD_MS
#define PLANET_ANIM_PERIOD_MS 20   // a new pass of each job every 20 ms (50 Hz, above the frame rate)
#endif
#ifndef PLANET_ANIM_UNITS
#define PLANET_ANIM_UNITS 16       // most units (LUT heights or texel rows of 64) per anim_step() call
#endif
int16_t g_sin[256];                // sin(2 pi i / 256) * 16384
bool g_sinOk = false;
volatile uint32_t g_commits = 0;   // gen_commit() count (written by the committing core, read by the animating one)
uint32_t g_baseFor = 0;            // the g_commits value the back-set copies belong to (0 = none)

// Work is counted in units: one LUT height, one cube texel row (6 faces x 64 rows), one tile row (grain 64 + hills 64).
// The caller's yield() is checked between units, so a sim launch or a planet chunk job waits for one unit at most
// (a few us), and the pass carries on from the same unit next time.
enum { JOB_LUT, JOB_CUBE, JOB_LAYERS, JOBS };
constexpr int JOB_UNITS[JOBS] = {256, 6 * N, 2 * DETAIL_N};
struct Job {
    int next;            // next unit of the pass in progress, -1 = idle
    uint32_t start, ms;  // when the pass started; ms = the animation time the whole pass uses
};
Job g_jobs[JOBS] = {{-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}};
int g_rr = 0;  // round robin over the jobs with a pass in progress

inline PLANET_HOT int S(uint32_t a) { return g_sin[a & 255u]; }  // Q14; a: 256 = one turn
inline PLANET_HOT uint32_t phaseOf(uint32_t ms, uint32_t periodMs) { return (ms % periodMs) * 256u / periodMs; }
inline PLANET_HOT int clampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline PLANET_HOT int cube3(int s) { return (((s * s) >> 14) * s) >> 14; }  // s^3 in Q14

// ---- LUT: one height of the colour table. m = brightness (Q8), wm = weight (Q8) of a colour mixed in.
PLANET_HOT void animRow(int palette, int h, uint32_t ms) {
    int r = g_base[h][0], g = g_base[h][1], b = g_base[h][2];
    int m = 256, wm = 0, wr = 0, wg = 0, wb = 0;
    // rip (Q8) = brightness ripple across the shades of this height: the grain tile shifts the shade of the ground
    // near the player, so an animated grain makes these ripples run across the water (or lava) itself
    int rip = 0;
    uint32_t ripT = 0;
    switch (palette) {
        case 0:  // terra: water shimmers and ripples, surf lines run in toward the coast (sea < 88, sand 96..104)
            if (h < 88) {
                m += (31 * S(phaseOf(ms, 1700) + (uint32_t)h * 6u)) >> 14;
                rip = 46;
                ripT = phaseOf(ms, 900);
            }
            if (h >= 66 && h < 104) {
                const int s = S(phaseOf(ms, 2600) - (uint32_t)h * 9u);
                const int nearQ8 = 256 - (h > 90 ? h - 90 : 90 - h) * 256 / 24;
                if (s > 0 && nearQ8 > 0) {
                    wm = (cube3(s) * nearQ8) >> 15;  // s^3 * near * 0.5
                    wr = 225; wg = 238; wb = 242;
                }
            }
            break;
        case 1:  // mars: slow dust waves
            m += (26 * S(phaseOf(ms, 5000) + (uint32_t)h * 3u)) >> 14;
            break;
        case 2: {  // ice: glints, each height at its own moment, and slow cold light bands sliding over the ice
            const int s = S(phaseOf(ms, 1100) + (uint32_t)h * 97u);
            if (s > 15974) {  // 0.975
                wm = ((s - 15974) * 28) >> 6;
                wr = 255; wg = 255; wb = 255;
            } else {
                const int band = S(phaseOf(ms, 4200) + (uint32_t)h * 5u);
                m += (20 * band) >> 14;
                if (band > 9830) {  // 0.6: a faint green-cyan tint at the crest
                    wm = ((band - 9830) * 70) >> 14;
                    wr = 150; wg = 255; wb = 225;
                }
            }
            break;
        }
        case 3:  // lava: the bright low ground flows and pulses, embers glow up in the crust
            if (h < 75) {
                m += (61 * (75 - h) / 75 * S(phaseOf(ms, 1400) - (uint32_t)h * 8u)) >> 14;
                rip = 40;  // heat shimmer
                ripT = phaseOf(ms, 700);
            }
            if (h >= 60) {
                const int s = S(phaseOf(ms, 2300) + (uint32_t)h * 4u);
                if (s > 0) {
                    wm = (72 * cube3(s)) >> 14;
                    wr = 255; wg = 96; wb = 24;
                }
            }
            break;
        case 4:  // alien: the teal band glows in a slow pulse running up the slopes
            if (h >= 90 && h < 180) {
                const int s = S(phaseOf(ms, 2200) - (uint32_t)h * 5u);
                m += (41 * s) >> 14;
                if (s > 0) {
                    wm = (64 * s) >> 14;
                    wr = 120; wg = 255; wb = 220;
                }
            }
            break;
        default:
            break;
    }
    if (wm > 0) {
        if (wm > 256) wm = 256;
        r += ((wr - r) * wm) >> 8;
        g += ((wg - g) * wm) >> 8;
        b += ((wb - b) * wm) >> 8;
    }
    // shade sh scales by sh / 24 = sh * 32 / 3 in Q8; lava's glow floor is 0.9 = 230 / 256 (as buildLut)
    const int R = (r * m) >> 8, G = (g * m) >> 8, B = (b * m) >> 8;
    const bool glow = palette == 3 && h < 60;
    uint16_t *row = &g_lut[h * SHADES];
    for (int sh = 0; sh < SHADES; ++sh) {
        int kq = sh * 32 / 3;
        if (glow && kq < 230) kq = 230;
        if (rip) kq += (kq * ((rip * S((uint32_t)sh * 40u + ripT)) >> 14)) >> 8;
        const int r8 = clampI((R * kq) >> 8, 0, 255), g8 = clampI((G * kq) >> 8, 0, 255), b8 = clampI((B * kq) >> 8, 0, 255);
        const uint16_t v = (uint16_t)(((r8 >> 3) << 11) | ((g8 >> 2) << 5) | (b8 >> 3));
        row[sh] = (uint16_t)((v >> 8) | (v << 8));
    }
}

// ---- CUBE: one texel row (unit = face * 64 + row). Texels whose still height is below `lim` (water, lava,
// pools) get two crossing waves; the phase comes from the texel's point on the cube surface (continuous across face
// edges), so the waves run around the whole world without seams. Land is never touched.
struct CubeAnim {
    int lim, top, amp;            // still height below lim moves; result kept <= top; amp = peak height change
    int shore;                    // still heights shore..lim-1 (just above the water line) only dip, never rise
    uint32_t per1, per2;          // wave periods (ms); a third, weaker wave runs at (per1 + per2) / 2 across both
    int k1[3], k2[3];             // wave directions (phase per half texel along x, y, z)
};
const CubeAnim CUBE_ANIM[PALETTES] = {
    {102, 96, 13, 88, 5200, 7300, {9, 5, 3}, {-4, 7, -6}},   // terra: sea waves; the beach (88..101) is washed over
    {84, 80, 9, 70, 11000, 15000, {3, 6, -4}, {-7, 2, 5}},   // mars: dust rolls through the dark basins (< 70)
    {84, 76, 7, 70, 8000, 10500, {-5, 4, 6}, {6, 5, -2}},    // ice: a slow swell under the deep ice (< 70)
    {90, 88, 10, 75, 9000, 12000, {5, 3, 7}, {-6, 2, 4}},    // lava: the edge of the bright lava (< 75) creeps
    {70, 66, 8, 60, 3000, 4100, {7, -3, 4}, {2, 6, -5}},     // alien: the dark pools (< 60) breathe
};

PLANET_HOT void cubeRow(int pal, int unit, uint32_t ms, Maps &F, const Maps &B) {
    const CubeAnim &A = CUBE_ANIM[pal];
    if (A.amp == 0) return;
    const int f = unit / N, j0 = unit % N;
    const int k = f >> 1, ka = (k + 1) % 3, kb = (k + 2) % 3;
    const int ck = (f & 1) ? -64 : 64;  // cube surface point in half texels: major axis +-64, others 2i-63 / 2j-63
    const uint32_t t1 = phaseOf(ms, A.per1), t2 = phaseOf(ms, A.per2), t3 = phaseOf(ms, (A.per1 + A.per2) / 2);
    const uint8_t *base = B.cube[f];
    uint8_t *dst = F.cube[f];
    for (int j = j0; j < j0 + 1; ++j) {
        const int cb = 2 * j - 63;
        // phase = c . kdir: the parts from the major axis and from j are fixed along the row
        const int row1 = ck * A.k1[k] + cb * A.k1[kb], row2 = ck * A.k2[k] + cb * A.k2[kb];
        const int d1 = 2 * A.k1[ka], d2 = 2 * A.k2[ka];
        int p1 = row1 - 63 * A.k1[ka], p2 = row2 - 63 * A.k2[ka];
        // third wave: direction k1 + k2 at twice the spatial frequency (finer ripples on the swell)
        const int d3 = 2 * (d1 + d2);
        int p3 = 2 * (p1 + p2);
        const uint8_t *bs = base + j * N;
        uint8_t *ds = dst + j * N;
        for (int i = 0; i < N; ++i, p1 += d1, p2 += d2, p3 += d3) {
            const int h = bs[i];
            if (h >= A.lim) continue;
            int w = ((S((uint32_t)p1 + t1) + S((uint32_t)p2 + t2) + (S((uint32_t)p3 - t3) >> 1)) * A.amp) >> 15;
            if (h >= A.shore) {  // land at the water line: dips under when a wave comes in, else stays as it is
                ds[i] = (uint8_t)(w < 0 ? h + w : h);
                continue;
            }
            ds[i] = (uint8_t)clampI(h + w, 0, A.top);
        }
    }
}

// ---- LAYERS: one row of the grain tile (units 0..63) or of the hills tile (64..127). Every
// pattern tiles with 64 texels (phase steps per texel are multiples of 4 = whole turns per tile).
PLANET_HOT void layerRow(int pal, int unit, uint32_t ms, Maps &F, const Maps &B) {
    constexpr int M = DETAIL_N - 1;
    constexpr int PLANET_ANIM_ROWS = 1;
    const bool hills = unit >= DETAIL_N;
    const int y0 = unit & M;
    const uint8_t *bt = hills ? B.hills : B.grain;
    uint8_t *dt = hills ? F.hills : F.grain;
    if (!hills) {
        switch (pal) {
            case 0: {  // terra: wind gusts through the grass, two directions
                const uint32_t t1 = phaseOf(ms, 2300), t2 = phaseOf(ms, 1300);
                for (int y = y0; y < y0 + PLANET_ANIM_ROWS; ++y)
                    for (int x = 0; x < DETAIL_N; ++x) {
                        const int g = ((bt[y * DETAIL_N + x] * 5) >> 3) + 48 + ((S((uint32_t)(x * 4 + y * 8) - t1) * 5) >> 10) +
                                      ((S((uint32_t)(x * 12 - y * 4) + t2) * 3) >> 11);
                        dt[y * DETAIL_N + x] = (uint8_t)clampI(g, 0, 255);
                    }
                return;
            }
            case 1: {  // mars: a dust layer drifts over the ground
                const int ox = (int)(ms / 60u) & M, oy = (int)(ms / 150u) & M;
                for (int y = y0; y < y0 + PLANET_ANIM_ROWS; ++y)
                    for (int x = 0; x < DETAIL_N; ++x)
                        dt[y * DETAIL_N + x] = (uint8_t)((bt[y * DETAIL_N + x] + bt[((y + oy) & M) * DETAIL_N + ((x + ox) & M)]) >> 1);
                return;
            }
            case 2: {  // ice: two layers of snow blow across at different speeds
                const int ox = (int)(ms / 35u) & M, oy = (int)(ms / 90u) & M, ox2 = (int)(ms / 55u) & M;
                for (int y = y0; y < y0 + PLANET_ANIM_ROWS; ++y)
                    for (int x = 0; x < DETAIL_N; ++x)
                        dt[y * DETAIL_N + x] = (uint8_t)((bt[y * DETAIL_N + x] * 2 + bt[((y + oy) & M) * DETAIL_N + ((x + ox) & M)] +
                                                          bt[((y - ox2) & M) * DETAIL_N + ((x + oy) & M)]) >> 2);
                return;
            }
            case 3: {  // lava: the crust texture streams along a wavy flow
                const int oy = (int)(ms / 80u) & M;
                const uint32_t t = phaseOf(ms, 3100);
                for (int y = y0; y < y0 + PLANET_ANIM_ROWS; ++y) {
                    const int wx = S((uint32_t)(y * 8) + t) >> 12;  // +-4 texels
                    for (int x = 0; x < DETAIL_N; ++x)
                        dt[y * DETAIL_N + x] = (uint8_t)((bt[y * DETAIL_N + x] + bt[((y + oy) & M) * DETAIL_N + ((x + wx) & M)]) >> 1);
                }
                return;
            }
            case 4: {  // alien: ripples cross the ground
                const uint32_t t = phaseOf(ms, 1800);
                for (int y = y0; y < y0 + PLANET_ANIM_ROWS; ++y)
                    for (int x = 0; x < DETAIL_N; ++x)
                        dt[y * DETAIL_N + x] = (uint8_t)clampI((bt[y * DETAIL_N + x] >> 1) + 64 + (S((uint32_t)(x * 8 + y * 8) - t) >> 8), 0, 255);
                return;
            }
            default:
                return;
        }
    }
    switch (pal) {
        case 1: {  // mars: the dunes migrate (one texel per 1.5 s, interpolated)
            const int q = (int)(ms / 1500u), fq = (int)((ms % 1500u) * 256u / 1500u);
            for (int y = y0; y < y0 + PLANET_ANIM_ROWS; ++y)
                for (int x = 0; x < DETAIL_N; ++x)
                    dt[y * DETAIL_N + x] = (uint8_t)((bt[y * DETAIL_N + ((x + q) & M)] * (256 - fq) + bt[y * DETAIL_N + ((x + q + 1) & M)] * fq) >> 8);
            return;
        }
        case 3: {  // lava: the lumps flow (the tile is warped by two slow waves, +-4 texels)
            const uint32_t t1 = phaseOf(ms, 4000), t2 = phaseOf(ms, 5300);
            for (int y = y0; y < y0 + PLANET_ANIM_ROWS; ++y) {
                const int wx = S((uint32_t)(y * 8) + t1) >> 12;
                for (int x = 0; x < DETAIL_N; ++x) {
                    const int wy = S((uint32_t)(x * 8) - t2) >> 12;
                    dt[y * DETAIL_N + x] = bt[((y + wy) & M) * DETAIL_N + ((x + wx) & M)];
                }
            }
            return;
        }
        case 4: {  // alien: the hills breathe in a wave running across them (+-12 %)
            const uint32_t t = phaseOf(ms, 2600);
            for (int y = y0; y < y0 + PLANET_ANIM_ROWS; ++y)
                for (int x = 0; x < DETAIL_N; ++x) {
                    const int a = bt[y * DETAIL_N + x] - 128;
                    dt[y * DETAIL_N + x] = (uint8_t)clampI(128 + ((a * (256 + (S((uint32_t)(x * 4 + y * 4) + t) >> 9))) >> 8), 0, 255);
                }
            return;
        }
        default:  // terra, ice: the hills stay still
            return;
    }
}
}  // namespace

bool PLANET_HOT anim_step(uint32_t ms, bool (*yield)()) {
    if (!g_sinOk) {  // once (float, from flash): the only non-integer step
        for (int i = 0; i < 256; ++i) g_sin[i] = (int16_t)lrintf(sinf((float)i * (6.2831853f / 256.0f)) * 16384.0f);
        g_sinOk = true;
    }
    const int front = g_front;
    Maps &F = g_sets[front];
    Maps &B = g_sets[1 - front];
    const int pal = F.palette;
    const uint32_t commits = g_commits;
    const bool backFree = commits != 0 && !g_gen.active && !g_gen.ready;
    if (backFree && g_baseFor != commits) {  // a new world on screen: keep a still copy of it in the idle back set
        memcpy(B.cube, F.cube, sizeof F.cube);
        memcpy(B.hills, F.hills, sizeof F.hills);
        memcpy(B.grain, F.grain, sizeof F.grain);
        g_baseFor = commits;
        g_jobs[JOB_CUBE].next = g_jobs[JOB_LAYERS].next = -1;
        return true;
    }
    const bool spatial = backFree && g_baseFor == commits;
    for (int j = 0; j < JOBS; ++j) {
        Job &J = g_jobs[j];
        if (J.next < 0 && (j == JOB_LUT || spatial) && (uint32_t)(ms - J.start) >= PLANET_ANIM_PERIOD_MS) {
            J.start = ms;
            J.ms = ms;
            J.next = 0;
        }
    }
    for (int n = 0; n < JOBS; ++n) {
        const int j = (g_rr + n) % JOBS;
        Job &J = g_jobs[j];
        if (J.next < 0) continue;
        if (j != JOB_LUT && !spatial) {  // a world build started: leave the back set alone
            J.next = -1;
            continue;
        }
        g_rr = j + 1;
        int u = J.next;
        for (int done = 0; done < PLANET_ANIM_UNITS && u < JOB_UNITS[j]; ++done, ++u) {
            if (done > 0 && yield && yield()) break;
            if (j == JOB_LUT) animRow(pal, u, J.ms);
            else if (j == JOB_CUBE) cubeRow(pal, u, J.ms, F, B);
            else layerRow(pal, u, J.ms, F, B);
        }
        J.next = u < JOB_UNITS[j] ? u : -1;
        return true;
    }
    return false;
}

bool gen_ready() { return g_gen.ready; }

void gen_commit() {
    if (!g_gen.ready) return;
    buildLut(g_sets[1 - g_front].palette);
    g_front = 1 - g_front;
    g_commits = g_commits + 1;  // the animator copies the new world into the back set before it moves anything
    g_gen.ready = false;
}

void generate(uint32_t seed, int style) {
    gen_begin(seed, style);
    while (!gen_step()) {
    }
    gen_commit();
}

// One hills-layer sample on the plane perpendicular to axis p: the texel (0..255) and the slope to
// the next texels (relief), world position H in 1/16 units, hillMul = texels per 1/16 unit.
static inline PLANET_HOT void hillSample(const uint8_t *hills, int p, float Hx, float Hy, float Hz, float hillMul, int &c00, int &rel) {
    const float hs = p == 0 ? Hy : (p == 1 ? Hz : Hx);
    const float ht = p == 0 ? Hz : (p == 1 ? Hx : Hy);
    const int du = (int)(hs * hillMul), dv = (int)(ht * hillMul);
    const int u0 = du & (DETAIL_N - 1), v0 = dv & (DETAIL_N - 1);
    const int u1 = (du + 1) & (DETAIL_N - 1), v1 = (dv + 1) & (DETAIL_N - 1);
    c00 = hills[v0 * DETAIL_N + u0];
    rel = (hills[v0 * DETAIL_N + u1] - c00) + (hills[v1 * DETAIL_N + u0] - c00);
}

void skin(int32_t lx, int32_t ly, int32_t bodyR, uint8_t rgb[3]) {
    const Maps &M = g_sets[g_front];
    const float inv = 1.0f / (float)(bodyR > 1 ? bodyR : 1);
    // The body spans about 50 of the 64 texels of the tile
    const float u = (float)lx * inv * PLANET_SKIN_TEXELS + 32.0f, v = (float)ly * inv * PLANET_SKIN_TEXELS + 32.0f;
    const float fu = floorf(u), fv = floorf(v);
    const int iu = (int)fu, iv = (int)fv;
    const int wu = (int)((u - fu) * 256.0f), wv = (int)((v - fv) * 256.0f);
    const int u0 = iu & (DETAIL_N - 1), v0 = iv & (DETAIL_N - 1), u1 = (iu + 1) & (DETAIL_N - 1), v1 = (iv + 1) & (DETAIL_N - 1);
    const int t0 = M.hills[v0 * DETAIL_N + u0] * (256 - wu) + M.hills[v0 * DETAIL_N + u1] * wu;
    const int t1 = M.hills[v1 * DETAIL_N + u0] * (256 - wu) + M.hills[v1 * DETAIL_N + u1] * wu;
    const int hv = (t0 * (256 - wv) + t1 * wv) >> 16;
    const int gv = M.grain[(((int)(v * 2.3f)) & (DETAIL_N - 1)) * DETAIL_N + (((int)(u * 2.3f)) & (DETAIL_N - 1))];
    int h = 128 + (((hv - 128) * 9) >> 2) + ((gv - 128) >> 1);  // more contrast than on the ground
    h = h < 0 ? 0 : (h > 255 ? 255 : h);
    const uint16_t sw = g_lut[h * SHADES + PLANET_SKIN_SHADE];
    const uint16_t c = (uint16_t)((sw >> 8) | (sw << 8));
    const unsigned r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    rgb[0] = (uint8_t)(r << 3 | r >> 2);
    rgb[1] = (uint8_t)(g << 2 | g >> 4);
    rgb[2] = (uint8_t)(b << 3 | b >> 2);
}

void setWideFu(float fu) { g_wideFu = fu; }

void sunDir(float out[3]) {
    const Maps &M = g_sets[g_front];
    out[0] = M.sun[0];
    out[1] = M.sun[1];
    out[2] = M.sun[2];
}

PLANET_HOT void render(const View &v, uint16_t *dst, int pitchPx, int y0, int rows, int width) {
    const float R = (float)v.radius16;
    const float ex = (float)v.eye[0], ey = (float)v.eye[1], ez = (float)v.eye[2];
    const double e2 = (double)v.eye[0] * v.eye[0] + (double)v.eye[1] * v.eye[1] + (double)v.eye[2] * v.eye[2];
    const float c = (float)(e2 - (double)v.radius16 * v.radius16);  // > 0 above the surface
    if (c <= 0) return;
    const float bF = ex * v.fwd[0] + ey * v.fwd[1] + ez * v.fwd[2];
    const float bR = ex * v.right[0] + ey * v.right[1] + ez * v.right[2];
    const float bU = ex * v.up[0] + ey * v.up[1] + ez * v.up[2];
    const float invF = 1.0f / v.focal, invR = 1.0f / R;
    // Fade starts later when the camera is far above the surface (flight): keep the far world visible
    const float altFU = (sqrtf((float)e2) - R) * (1.0f / FU16);
    const float excess = altFU > 40.0f ? altFU - 40.0f : 0.0f;
    const float fogNear = PLANET_FOG_NEAR_FU + excess, fogInv = 1.0f / (PLANET_FOG_FAR_FU - PLANET_FOG_NEAR_FU);
    const float hillInv = 1.0f / PLANET_HILL_FADE_FU, grainInv = 1.0f / PLANET_GRAIN_FADE_FU;
    const float hillMul = (float)DETAIL_N / (PLANET_HILL_TILE_FU * FU16);
    const float grainMul = (float)DETAIL_N / (PLANET_GRAIN_TILE_FU * FU16);
    const Maps &M = g_sets[g_front];
    const float sx = M.sun[0], sy = M.sun[1], sz = M.sun[2];

    for (int ry = 0; ry < rows; ry += PLANET_BLOCK_H) {
        const float syn = (v.cy - ((float)(y0 + ry) + 0.5f * PLANET_BLOCK_H)) * invF;
        const float Drx = v.fwd[0] + syn * v.up[0], Dry = v.fwd[1] + syn * v.up[1], Drz = v.fwd[2] + syn * v.up[2];
        const float bRow = bF + syn * bU;
        const float aRow = 1.0f + syn * syn;
        uint16_t *row0 = dst + (size_t)ry * pitchPx;
        int stepW = PLANET_BLOCK_W;
        for (int x = 0; x < width; x += stepW) {
            stepW = PLANET_BLOCK_W;
            const float sxn = ((float)x + 0.5f * PLANET_BLOCK_W - v.cx) * invF;
            const float b = bRow + sxn * bR;
            if (b >= 0) continue;
            const float a = aRow + sxn * sxn;
            const float disc = b * b - a * c;
            if (disc <= 0) continue;
            const float t = c / (sqrtf(disc) - b);
            const float Dx = Drx + sxn * v.right[0], Dy = Dry + sxn * v.right[1], Dz = Drz + sxn * v.right[2];
            const float Hx = ex + t * Dx, Hy = ey + t * Dy, Hz = ez + t * Dz;
            const float nx = Hx * invR, ny = Hy * invR, nz = Hz * invR;
            // distance to the hit in FU: |D| = sqrt(a) with a = 1 + sx^2 + sy^2 <= 2.5, so (1 + a) / 2 is within 10 %
            const float dist = t * (1.0f + a) * (0.5f / FU16);

            // --- height from the cube map (bilinear)
            const float ax = fabsf(nx), ay = fabsf(ny), az = fabsf(nz);
            int k, k2;
            float major, second, s, tt;
            if (ax >= ay && ax >= az) {
                k = 0;
                major = ax;
                s = ny;
                tt = nz;
                k2 = ay >= az ? 1 : 2;
                second = ay >= az ? ay : az;
            } else if (ay >= az) {
                k = 1;
                major = ay;
                s = nz;
                tt = nx;
                k2 = ax >= az ? 0 : 2;
                second = ax >= az ? ax : az;
            } else {
                k = 2;
                major = az;
                s = nx;
                tt = ny;
                k2 = ax >= ay ? 0 : 1;
                second = ax >= ay ? ax : ay;
            }
            const float nk = k == 0 ? nx : (k == 1 ? ny : nz);
            const uint8_t *face = M.cube[(k << 1) | (nk < 0 ? 1 : 0)];
            const float inv = 1.0f / major;
            float fu = (s * inv * 0.5f + 0.5f) * N - 0.5f, fv = (tt * inv * 0.5f + 0.5f) * N - 0.5f;
            if (fu < 0) fu = 0;
            if (fv < 0) fv = 0;
            if (fu > N - 1.001f) fu = N - 1.001f;
            if (fv > N - 1.001f) fv = N - 1.001f;
            const int iu = (int)fu, iv = (int)fv;
            const int wu = (int)((fu - (float)iu) * 256.0f), wv = (int)((fv - (float)iv) * 256.0f);
            const uint8_t *p = face + iv * N + iu;
            const int top = p[0] * (256 - wu) + p[1] * wu;
            const int bot = p[N] * (256 - wu) + p[N + 1] * wu;
#ifdef PLANET_NEAREST
            int h = p[0];
            (void)top; (void)bot; (void)wv;
#else
            int h = (top * (256 - wv) + bot * wv) >> 16;
#endif

            // --- ground layers, tiled in world space on the dominant plane, fading with distance.
            // Near a cube edge (two axes about equal) the hills of the two planes are cross-faded,
            // otherwise the pattern would crease where the dominant plane switches.
            const float hs = k == 0 ? Hy : (k == 1 ? Hz : Hx);
            const float ht = k == 0 ? Hz : (k == 1 ? Hx : Hy);
            const float wh = 1.0f - dist * hillInv;
            int relief = 0, grain = 0;
#ifdef PLANET_NO_DETAIL
            if (false) {
#else
            if (wh > 0.0f) {
#endif
                int c00, rel;
                hillSample(M.hills, k, Hx, Hy, Hz, hillMul, c00, rel);
                const float ratio = second * inv;
                if (ratio > 0.78f) {
                    int c2, rel2;
                    hillSample(M.hills, k2, Hx, Hy, Hz, hillMul, c2, rel2);
                    float w2 = (ratio - 0.78f) * (0.5f / 0.22f);  // 0 .. 0.5 at the edge itself
                    const int q = (int)(w2 * 256.0f);
                    c00 = (c00 * (256 - q) + c2 * q) >> 8;
                    rel = (rel * (256 - q) + rel2 * q) >> 8;
                }
                const int dhh = (int)((c00 - 128) * wh);
                h += (dhh * 3) >> 2;
                if (h < 0) h = 0;
                if (h > 255) h = 255;
                relief = (int)(rel * wh);
            }
            const float wg = 1.0f - dist * grainInv;
#ifdef PLANET_NO_DETAIL
            if (false) {
#else
            if (wg > 0.0f) {
#endif
                const int gu = (int)(hs * grainMul) & (DETAIL_N - 1), gv = (int)(ht * grainMul) & (DETAIL_N - 1);
                grain = (int)(((int)M.grain[gv * DETAIL_N + gu] - 128) * wg);
            }

            // --- light and fade
            float lit = nx * sx + ny * sy + nz * sz;
            lit = lit < 0 ? 0 : lit;
            float fog = (PLANET_FOG_FAR_FU + excess - dist) * fogInv;
            fog = fog < 0 ? 0 : (fog > 1 ? 1 : fog);
            (void)fogNear;
            int shade = (int)((0.42f + 0.62f * lit) * 24.0f * fog) + (relief >> 4) + (grain >> 5);
            if (shade < 0) shade = 0;
            if (shade > SHADES - 1) shade = SHADES - 1;
            const uint16_t px = g_lut[h * SHADES + shade];

            if (dist > g_wideFu) stepW = 2 * PLANET_BLOCK_W;
            uint16_t *o = row0 + x;
            const int xe = x + stepW > width ? width - x : stepW;
            for (int yy = 0; yy < PLANET_BLOCK_H && ry + yy < rows; ++yy) {
                for (int xx = 0; xx < xe; ++xx) o[yy * pitchPx + xx] = px;
            }
            ++g_blocks;
        }
    }
}

}  // namespace planet
