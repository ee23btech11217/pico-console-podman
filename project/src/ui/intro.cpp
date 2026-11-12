#include "ui/intro.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>

#include "input/buttons.h"
#include "shapoco/gfx2d/fonts.hpp"
#include "world/planet_core.h"

#ifndef INTRO_RESERVE_BYTES
#define INTRO_RESERVE_BYTES 2048  // heap left for newlib (stdio buffers, printf) and growth
#endif
#ifndef INTRO_STAR_MAX
#define INTRO_STAR_MAX 4096
#endif
#ifndef INTRO_CUTSCENE_MS
#define INTRO_CUTSCENE_MS 5200
#endif

extern "C" {
extern char __HeapLimit;
void *sbrk(ptrdiff_t incr);
}

namespace g2 = shapoco::gfx2d;

namespace intro {

namespace {

constexpr int SW = 320, SH = 240;
constexpr float R16 = 2097152.0f;  // world radius in 1/16 units
constexpr float FOCAL = 171.0f;

struct World {
    const char *name, *blurb;
    uint8_t r, g, b;  // accent colour
};
const World WORLDS_[WORLDS] = {
    {"TERRA", "GREEN LANDS AND BLUE LAKES", 110, 220, 130},
    {"MARS", "RUST DESERTS AND DUST", 255, 150, 90},
    {"GLACIER", "ICE FIELDS UNDER COLD LIGHT", 170, 225, 255},
    {"INFERNO", "RIVERS OF LAVA ON BLACK ROCK", 255, 110, 50},
    {"XENOS", "VIOLET SEAS AND TEAL SHORES", 210, 110, 255},
};

// ----------------------------------------------------------------- state
Screen g_screen = Screen::NONE;
int g_sel = 0;           // highlighted world in the select screen
int g_style = 0;         // chosen world
int g_cutStyle = 0;      // world of the cut scene
uint32_t g_t0 = 0;       // when the current screen started
uint32_t g_now = 0;      // animation clock (ms), set by tick()
uint32_t g_lastTick = 0;
int g_preview = -1;
uint16_t g_prevMask = 0xFFFF;
uint32_t g_fadeStart = 0;
bool g_fading = false;
uint32_t g_rng = 0x2F6E2B1u;

// The star field: 8 bytes a star, as many as the free RAM allows (see allocStars)
struct Star {
    int16_t x, y;
    uint16_t z;
    uint8_t bright;
    uint8_t tint;
};
static_assert(sizeof(Star) == 8, "Star must stay 8 bytes");
Star *g_stars = nullptr;
uint32_t g_nStars = 0;
Footprint g_fp = {};
int g_streakDz = 0;  // z units a star travelled over the "tail" of its streak this frame
int g_speed = 0;     // z units per second

uint32_t rnd() {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

void respawn(Star &s, bool far) {
    const uint32_t r = rnd();
    s.x = (int16_t)((int)(r & 0xFFF) - 2048) * 3 / 4;
    s.y = (int16_t)((int)((r >> 12) & 0xFFF) - 2048) * 3 / 4;
    s.z = far ? (uint16_t)(1100 + (rnd() % 500)) : (uint16_t)(30 + (rnd() % 1500));
    s.bright = (uint8_t)(110 + (rnd() & 127));
    s.tint = (uint8_t)(rnd() & 3);
}

// Take what is left of the heap, minus a reserve, and make it stars.
void allocStars() {
    if (g_stars) return;
    // The SDK's sbrk is optimistic: the first malloc already took the whole heap up to the top of RAM, so the free
    // bytes are inside malloc's own free chunk (mallinfo), plus anything the break has not reached yet.
    const uintptr_t cur = (uintptr_t)sbrk(0);
    const uintptr_t lim = (uintptr_t)&__HeapLimit;
    const struct mallinfo mi = mallinfo();
    const uint32_t freeB = (uint32_t)mi.fordblks + (lim > cur ? (uint32_t)(lim - cur) : 0);
    const uint32_t reserve = INTRO_RESERVE_BYTES;
    const uint32_t usable = freeB > reserve + 64 ? freeB - reserve - 64 : 0;  // 64: malloc's own bookkeeping
    uint32_t n = usable / sizeof(Star);
    if (n > INTRO_STAR_MAX) n = INTRO_STAR_MAX;
    g_stars = n ? (Star *)malloc(n * sizeof(Star)) : nullptr;
    g_nStars = g_stars ? n : 0;
    g_fp = {freeB, reserve, (uint32_t)(g_nStars * sizeof(Star)), g_nStars};
    for (uint32_t i = 0; i < g_nStars; ++i) respawn(g_stars[i], false);
    printf("INTRO stars %lu = %lu bytes of %lu free (reserve %lu)\n", (unsigned long)g_fp.stars, (unsigned long)g_fp.bytes,
           (unsigned long)g_fp.heapFree, (unsigned long)g_fp.reserve);
}

void freeStars() {
    if (!g_stars) return;
    free(g_stars);
    g_stars = nullptr;
    g_nStars = 0;
}

inline float smooth(float t) {
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return t * t * (3.0f - 2.0f * t);
}
inline float easeOut(float t) {
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
}
inline uint16_t pack(int r, int g, int b) {
    r = r < 0 ? 0 : (r > 255 ? 255 : r);
    g = g < 0 ? 0 : (g > 255 ? 255 : g);
    b = b < 0 ? 0 : (b > 255 ? 255 : b);
    const uint16_t v = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    return (uint16_t)((v >> 8) | (v << 8));
}
inline g2::Color dim(const World &w, float f) {
    f = f < 0 ? 0 : (f > 1 ? 1 : f);
    return g2::makeColor((int)(w.r * f), (int)(w.g * f), (int)(w.b * f));
}
inline g2::Color whiteDim(float f) {
    f = f < 0 ? 0 : (f > 1 ? 1 : f);
    return g2::makeColor((int)(255 * f), (int)(255 * f), (int)(255 * f));
}

// ----------------------------------------------------------------- camera for the globe
planet::View makeView(float ang, float tilt, float distR, float cy) {
    planet::View v{};
    const float d = distR * R16;
    const float ex = d * cosf(tilt) * cosf(ang), ey = d * sinf(tilt), ez = d * cosf(tilt) * sinf(ang);
    v.eye[0] = (int32_t)ex;
    v.eye[1] = (int32_t)ey;
    v.eye[2] = (int32_t)ez;
    const float il = 1.0f / sqrtf(ex * ex + ey * ey + ez * ez);
    const float fx = -ex * il, fy = -ey * il, fz = -ez * il;
    // right = fwd x worldUp (0,1,0), then up = right x fwd
    float rx = fy * 0 - fz * 1, ry = fz * 0 - fx * 0, rz = fx * 1 - fy * 0;
    const float rl = 1.0f / sqrtf(rx * rx + ry * ry + rz * rz);
    rx *= rl; ry *= rl; rz *= rl;
    const float ux = ry * fz - rz * fy, uy = rz * fx - rx * fz, uz = rx * fy - ry * fx;
    v.fwd[0] = fx; v.fwd[1] = fy; v.fwd[2] = fz;
    v.right[0] = rx; v.right[1] = ry; v.right[2] = rz;
    v.up[0] = ux; v.up[1] = uy; v.up[2] = uz;
    v.focal = FOCAL;
    v.cx = SW * 0.5f;
    v.cy = cy;
    v.radius16 = (int32_t)R16;
    return v;
}

// ----------------------------------------------------------------- drawing helpers
struct Band {
    uint16_t *px;  // first row of the band
    int pitch;
    int y, h, w;   // screen row of the first row, rows, width
};

inline void plot(const Band &b, int x, int sy, uint16_t col) {
    const int r = sy - b.y;
    if (r < 0 || r >= b.h || x < 0 || x >= b.w) return;
    uint16_t *p = b.px + r * b.pitch + x;
    if (*p == 0) *p = col;  // only on empty space: the globe stays in front
}

void line(const Band &b, int x0, int y0, int x1, int y1, uint16_t col) {
    if ((x0 < 0 && x1 < 0) || (x0 >= b.w && x1 >= b.w) || (y0 < b.y && y1 < b.y) || (y0 >= b.y + b.h && y1 >= b.y + b.h)) return;
    int dx = x1 - x0, dy = y1 - y0;
    int n = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
    if (n == 0) {
        plot(b, x0, y0, col);
        return;
    }
    if (n > 160) n = 160;
    for (int i = 0; i <= n; ++i) plot(b, x0 + dx * i / n, y0 + dy * i / n, col);
}

void drawStars(const Band &b, bool streaks, const World &w) {
    for (uint32_t i = 0; i < g_nStars; ++i) {
        const Star &s = g_stars[i];
        if (s.z < 12) continue;
        const int sx = SW / 2 + (int)s.x * 170 / s.z, sy = SH / 2 + (int)s.y * 170 / s.z;
        // brighter the nearer; a quarter of the stars carry the world's colour
        int bright = s.bright * (1500 - (int)s.z) / 1500;
        if (bright < 24) bright = 24;
        const bool tinted = (s.tint == 0);
        const uint16_t col = tinted ? pack(w.r * bright / 255, w.g * bright / 255, w.b * bright / 255)
                                    : pack(bright * 200 / 255, bright * 225 / 255, bright);
        if (streaks && g_streakDz > 0) {
            const int z0 = (int)s.z + g_streakDz;
            const int tx = SW / 2 + (int)s.x * 170 / z0, ty = SH / 2 + (int)s.y * 170 / z0;
            line(b, tx, ty, sx, sy, col);
        } else {
            plot(b, sx, sy, col);
            if (s.z < 500) plot(b, sx + 1, sy, col);
        }
    }
}

template <class F>
int textWidth(g2::Graphics2D &g, F *font, const char *s) {
    g.setFont(font);
    return g.textMetrics(s).width;
}

template <class F>
void textAt(g2::Graphics2D &g, int oy, F *font, int x, int y, const char *s, g2::Color c) {
    g.setFont(font);
    g.setTextColor(g2::makeColor(0, 0, 0));
    g.drawString(x + 1, y + oy + 1, s);
    g.setTextColor(c);
    g.drawString(x, y + oy, s);
}

template <class F>
void textCentered(g2::Graphics2D &g, int oy, F *font, int y, const char *s, g2::Color c) {
    textAt(g, oy, font, (SW - textWidth(g, font, s)) / 2, y, s, c);
}

// ----------------------------------------------------------------- the two screens
void drawSelect(const Band &b, g2::Graphics2D &g, int oy) {
    const World &w = WORLDS_[g_sel];
    const float tsec = (float)(g_now - g_t0) * 0.001f;
    const planet::View v = makeView(0.5f + tsec * 0.32f, 0.38f, 2.7f, 106.0f);
    planet::render(v, b.px, b.pitch, b.y, b.h, SW);
    drawStars(b, false, w);

    textCentered(g, oy, &g2::ShapoSansP_s12c09a01w02, 6, "CHOOSE YOUR WORLD", whiteDim(0.95f));
    textCentered(g, oy, &g2::ShapoSansP_s21c16a01w03, 184, w.name, g2::makeColor(w.r, w.g, w.b));
    textCentered(g, oy, &g2::ShapoSansMono_s08c07, 211, w.blurb, whiteDim(0.7f));
    textCentered(g, oy, &g2::ShapoSansMono_s08c07, 226, "< > CHOOSE    A START    B BACK", whiteDim(0.55f));
    textAt(g, oy, &g2::ShapoSansP_s21c16a01w03, 14, 100, "<", whiteDim(0.8f));
    textAt(g, oy, &g2::ShapoSansP_s21c16a01w03, SW - 28, 100, ">", whiteDim(0.8f));
    // one dot per world, the highlighted one filled and in its colour
    for (int i = 0; i < WORLDS; ++i) {
        const int cx = SW / 2 + (i - WORLDS / 2) * 14, cy = 172;
        const bool on = i == g_sel;
        const g2::Color c = on ? g2::makeColor(WORLDS_[i].r, WORLDS_[i].g, WORLDS_[i].b) : whiteDim(0.35f);
        if (on) g.fillRect(cx - 3, cy - 3 + oy, 7, 7, c);
        else g.drawRect(cx - 2, cy - 2 + oy, 5, 5, c, 1);
    }
}

void typed(char *dst, size_t cap, const char *s, int shown) {
    const int n = (int)strlen(s);
    if (shown > n) shown = n;
    if (shown >= (int)cap) shown = (int)cap - 1;
    memcpy(dst, s, (size_t)shown);
    dst[shown] = 0;
}

void drawCutscene(const Band &b, g2::Graphics2D &g, int oy) {
    const World &w = WORLDS_[g_cutStyle];
    const float t = (float)(g_now - g_t0);  // ms
    const float T = (float)INTRO_CUTSCENE_MS;

    // The globe: invisible during the warp, then a dot that swells, then a dive
    if (t >= 1500) {
        const float a = 0.4f + t * 0.00026f;
        float distR, tilt;
        if (t < 3700) {
            const float k = easeOut((t - 1500) / 2200.0f);
            distR = 40.0f + (2.4f - 40.0f) * k;
            tilt = 0.32f;
            planet::setWideFu(30.0f);
        } else {
            const float k = smooth((t - 3700) / 1500.0f);
            distR = 2.4f + (1.07f - 2.4f) * k;
            tilt = 0.32f + 0.75f * k;
            planet::setWideFu(k > 0.35f ? 0.0f : 30.0f);  // the world fills the screen: paint it in coarser blocks
        }
        const planet::View v = makeView(a, tilt, distR, 120.0f);
        planet::render(v, b.px, b.pitch, b.y, b.h, SW);
    }
    drawStars(b, t < 3600, w);

    // Cinematic bars slide in, hold, and open for the last second
    float bar = 26.0f * smooth(t / 500.0f);
    if (t > T - 900.0f) bar *= 1.0f - smooth((t - (T - 900.0f)) / 700.0f);
    const int barH = (int)bar;
    for (int r = 0; r < b.h; ++r) {
        const int sy = b.y + r;
        if (sy < barH || sy >= SH - barH) memset(b.px + r * b.pitch, 0, (size_t)SW * 2);
    }

    // Subtitles in the bars, typed out
    char buf[48];
    const int tl = (int)t;
    if (tl > 400 && tl < 1900) {
        typed(buf, sizeof buf, "HYPERSPACE JUMP", (tl - 400) / 40);
        textCentered(g, oy, &g2::ShapoSansP_s12c09a01w02, 7, buf, dim(w, 0.9f));
    }
    if (tl > 1100 && tl < 3100) {
        typed(buf, sizeof buf, "ENTERING A NEW SPHERE", (tl - 1100) / 45);
        textCentered(g, oy, &g2::ShapoSansP_s12c09a01w02, SH - 20, buf, whiteDim(0.9f));
    }
    if (tl > 2900 && tl < 4700) {
        const float f = tl < 3300 ? (tl - 2900) / 400.0f : (tl > 4300 ? (4700 - tl) / 400.0f : 1.0f);
        textCentered(g, oy, &g2::ShapoSansP_s27c22a01w04, 82, w.name, dim(w, f));
        textCentered(g, oy, &g2::ShapoSansMono_s08c07, 120, w.blurb, whiteDim(0.8f * f));
    }
    if (tl > 3500) {
        typed(buf, sizeof buf, "DEVOUR TO GROW", (tl - 3500) / 45);
        textCentered(g, oy, &g2::ShapoSansP_s12c09a01w02, SH - 20, buf, whiteDim(0.95f));
    }
}

}  // namespace

// ----------------------------------------------------------------- API
Screen screen() { return g_screen; }
int style() { return g_style; }
int selection() { return g_sel; }
int styleForLevel(int level) { return (g_style + (level > 1 ? level - 1 : 0)) % WORLDS; }
const char *worldName(int s) { return WORLDS_[((s % WORLDS) + WORLDS) % WORLDS].name; }
Footprint footprint() { return g_fp; }
uint32_t previewSeed(int s) { return 0x5EED0000u + (uint32_t)s * 7919u; }

int takePreviewRequest() {
    const int p = g_preview;
    g_preview = -1;
    return p;
}

void openWorldSelect(uint32_t nowMs) {
    allocStars();
    g_screen = Screen::WORLD_SELECT;
    g_sel = g_style;
    g_t0 = g_now = g_lastTick = nowMs;
    g_speed = 22;
    g_streakDz = 0;
    g_preview = g_sel;  // show the current choice right away
    g_prevMask = 0xFFFF;
}

void startCutscene(uint32_t nowMs, int levelStyle) {
    allocStars();
    g_screen = Screen::CUTSCENE;
    g_cutStyle = levelStyle % WORLDS;
    g_t0 = g_now = g_lastTick = nowMs;
    g_speed = 0;
    g_prevMask = 0xFFFF;
    for (uint32_t i = 0; i < g_nStars; ++i) respawn(g_stars[i], false);
}

Event input(uint16_t mask, uint32_t nowMs) {
    // B (GP11) reads as pressed all the time on this console and is ignored
    mask &= (uint16_t) ~(1u << BTN_IDX_B);
    const uint16_t prev = g_prevMask == 0xFFFF ? mask : g_prevMask;
    const uint16_t edge = mask & (uint16_t)~prev;
    g_prevMask = mask;
    const uint16_t confirm = (1u << BTN_IDX_X) | (1u << BTN_IDX_A);
    if (g_screen == Screen::WORLD_SELECT) {
        if (edge & (1u << BTN_IDX_LEFT)) {
            g_sel = (g_sel + WORLDS - 1) % WORLDS;
            g_preview = g_sel;
        } else if (edge & (1u << BTN_IDX_RIGHT)) {
            g_sel = (g_sel + 1) % WORLDS;
            g_preview = g_sel;
        }
        if (edge & confirm) {
            g_style = g_sel;
            g_screen = Screen::NONE;
            freeStars();
            return Event::CONFIRM;
        }
        if (edge & (1u << BTN_IDX_Y)) {
            g_screen = Screen::NONE;
            freeStars();
            return Event::CANCEL;
        }
    } else if (g_screen == Screen::CUTSCENE) {
        if ((edge & confirm) && nowMs - g_t0 > 600) {
            g_t0 = nowMs - (uint32_t)INTRO_CUTSCENE_MS;  // jump to the end; tick() finishes it
            return Event::SKIP;
        }
    }
    return Event::NONE;
}

Event tick(uint32_t nowMs) {
    if (g_screen == Screen::NONE) return Event::NONE;
    const uint32_t dtMs = nowMs - g_lastTick > 100 ? 100 : nowMs - g_lastTick;
    g_lastTick = nowMs;
    g_now = nowMs;
    if (g_screen == Screen::CUTSCENE) {
        const float t = (float)(nowMs - g_t0);
        if (t >= (float)INTRO_CUTSCENE_MS) {
            g_screen = Screen::NONE;
            freeStars();
            g_fadeStart = nowMs;
            g_fading = true;
            return Event::CUTSCENE_DONE;
        }
        // warp speed: ramp up fast, hold, brake as the world appears
        float v;
        if (t < 1500) v = 150.0f + 5200.0f * smooth(t / 1300.0f);
        else if (t < 3300) v = 5350.0f * (1.0f - smooth((t - 1500.0f) / 1800.0f)) + 260.0f;
        else v = 260.0f - 150.0f * smooth((t - 3300.0f) / 1500.0f);
        g_speed = (int)v;
        g_streakDz = t < 3600 ? (int)(v * 0.0045f) : 0;
    }
    const int dz = g_speed * (int)dtMs / 1000;
    for (uint32_t i = 0; i < g_nStars; ++i) {
        Star &s = g_stars[i];
        if (s.z <= dz + 14) respawn(s, true);
        else s.z = (uint16_t)(s.z - dz);
    }
    return Event::NONE;
}

int fadeInPermille(uint32_t nowMs) {
    if (!g_fading) return 0;
    const uint32_t e = nowMs - g_fadeStart;
    if (e >= 450) {
        g_fading = false;
        return 0;
    }
    return (int)(1000 - e * 1000 / 450);
}

void fadeBand(uint16_t *px, int pitch, int rows, int permille) {
    const int keep = 1000 - permille;  // brightness kept, 0..1000
    for (int r = 0; r < rows; ++r) {
        uint16_t *p = px + r * pitch;
        for (int x = 0; x < SW; ++x) {
            const uint16_t sw = p[x];
            const uint16_t c = (uint16_t)((sw >> 8) | (sw << 8));
            const int R = ((c >> 11) & 31) * keep / 1000, G = ((c >> 5) & 63) * keep / 1000, B = (c & 31) * keep / 1000;
            const uint16_t v = (uint16_t)((R << 11) | (G << 5) | B);
            p[x] = (uint16_t)((v >> 8) | (v << 8));
        }
    }
}

bool band(const g2::Surface &dst, int y, int h, int dstY) {
    if (g_screen == Screen::NONE) return false;
    Band b{(uint16_t *)dst.linePtr(dstY), (int)(dst.stride / 2), y, h, dst.width};
    g2::Graphics2D g(dst);
    g.setClipRect(0, dstY, dst.width, h);
    const int oy = dstY - y;
    if (g_screen == Screen::WORLD_SELECT) drawSelect(b, g, oy);
    else drawCutscene(b, g, oy);
    return true;
}

}  // namespace intro
