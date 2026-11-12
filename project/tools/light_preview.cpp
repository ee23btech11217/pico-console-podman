// Host preview of Round 17 body lighting: the same game frame with the bodies flat (off) and lit by the world's
// sun (on), with the real renderer, the real planet surface and the player's skin.
//   light_preview <outdir> [seed] [style] [ticks] [ambient%] [sun%]  -> light_off.ppm, light_on.ppm
// Build (project root; C = third_party/devoursphere_core, G = /opt/devour-sphere/submodule/shapo-gfx):
//   g++ -O1 -std=c++17 -w -DPLANET_HOST -DDEVOURSPHERE_TICK_RATE=60 -DSHAPOGFX_FORMAT_GRAY1=0 -DSHAPOGFX_FORMAT_RGB444=0 \
//       -DSHAPOGFX_FORMAT_ARGB4444=0 -DSHAPOGFX_FORMAT_RGB565_SWAPPED=1 -DSHAPOGFX2D_TRANSFORM=0 -DSHAPOGFX2D_COLOR_KEY=0 \
//       -DSHAPOGFX3D_TEXTURE=0 -I$C/include -I$G/include -Isrc tools/light_preview.cpp src/world/planet_core.cpp \
//       $C/src/sim/*.cpp $C/src/render/*.cpp $G/src/gfx2d/*.cpp $G/src/gfx3d/*.cpp -o light_preview
// Lighting levels as on the board (planet.cpp BODY_LIGHT_AMB / BODY_LIGHT_SUN, percent).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "devoursphere/devoursphere.hpp"
#include "world/planet_core.h"

namespace sim = devoursphere::sim;
namespace render = devoursphere::render;
namespace g2 = shapoco::gfx2d;

alignas(16) static uint8_t arena[64 * 1024];
static sim::Game game;
static render::Renderer rend;

static void band_hook(const render::Renderer::BackdropView &bv, const g2::Surface &dst, int y, int h, int dstY) {
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
    planet::render(v, (uint16_t *)dst.linePtr(dstY), (int)(dst.stride / 2), y, h, dst.width);
}

// same as src/world/planet.cpp skin_hook (PLANET_SKIN_MIX 62)
static bool skin_hook(bool isPlayer, g2::Color base, int32_t lx, int32_t ly, int32_t bodyR, g2::Color &out) {
    if (!isPlayer) return false;
    uint8_t w[3];
    planet::skin(lx, ly, bodyR, w);
    const int mix = 62;
    int r = (w[0] * mix + g2::colorR(base) * (100 - mix)) / 100;
    int g = (w[1] * mix + g2::colorG(base) * (100 - mix)) / 100;
    int b = (w[2] * mix + g2::colorB(base) * (100 - mix)) / 100;
    r += r >> 3;
    g += g >> 3;
    b += b >> 3;
    out = g2::makeColor(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b);
    return true;
}

static bool light_hook(float toSun[3]) {
    planet::sunDir(toSun);
    return true;
}

static void shot(const char *dir, const char *name) {
    const int W = 320, H = 240, BH = 40;
    static uint16_t band[320 * 40];
    std::vector<uint16_t> fb((size_t)W * H);
    rend.beginFrame(game, 0.0f);
    for (int b = 0; b < H / BH; ++b) {
        g2::Surface s = g2::makeSurface(g2::PixelFormat::RGB565_SWAPPED, W, BH, band);
        rend.renderBand(s, b * BH, BH, 0);
        memcpy(fb.data() + (size_t)b * BH * W, band, sizeof band);
    }
    rend.endFrame();
    char p[256];
    snprintf(p, sizeof p, "%s/light_%s.ppm", dir, name);
    FILE *f = fopen(p, "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (uint16_t px : fb) {
        const uint16_t c = (uint16_t)((px >> 8) | (px << 8));
        const unsigned r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
        fputc((int)(r << 3 | r >> 2), f);
        fputc((int)(g << 2 | g >> 4), f);
        fputc((int)(b << 3 | b >> 2), f);
    }
    fclose(f);
}

static void run(int n, sim::Input in) {
    for (int i = 0; i < n; ++i) {
        game.tick(in);
        rend.pollEffects(game);
    }
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    const uint32_t seed = argc > 2 ? (uint32_t)strtoul(argv[2], nullptr, 0) : 12345u;
    const int style = argc > 3 ? atoi(argv[3]) : 0;
    const int ticks = argc > 4 ? atoi(argv[4]) : 900;
    planet::generate(seed, style);
    game.reset(seed);
    rend.init(320, 240, arena, sizeof arena);
    rend.setBandHook(band_hook);
    rend.setSkinHook(skin_hook);
    rend.setWireAlpha(0);
    run(30, sim::Input());
    run(3, sim::Input(sim::Button::A));  // title -> weapon select
    run(10, sim::Input());
    run(3, sim::Input(sim::Button::A));  // weapon select -> arrival
    // arrival, then play: steer in a slow circle and fire so bodies come into view
    for (int i = 0; i < ticks; ++i) {
        sim::Input in((int8_t)((i / 120) % 2 ? 60 : -60), (int8_t)-40, (uint8_t)((i & 8) ? sim::Button::A : 0));
        game.tick(in);
        rend.pollEffects(game);
    }
    for (int k = 0; k < 4; ++k) rend.beginFrame(game, 1.0f / 60), rend.endFrame();  // settle the camera
    shot(dir, "off");
    const float amb = argc > 5 ? atoi(argv[5]) / 100.0f : 0.60f, sun = argc > 6 ? atoi(argv[6]) / 100.0f : 0.60f;
    rend.setBodyLightHook(light_hook, amb, sun);
    shot(dir, "on");
    printf("state %d, entities alive %d, palette %d\n", (int)game.state(), game.aliveEntities(), planet::paletteIndex());
    return 0;
}
