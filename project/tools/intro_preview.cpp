// Host preview of the world select screen and the cut scene: renders frames of intro.cpp with the real gfx2d library.
//   intro_preview <outdir>   -> select_N.ppm (5 worlds), cut_<ms>.ppm
// Build (from the project root, G = /opt/devour-sphere/submodule/shapo-gfx):
//   g++ -O2 -std=c++17 -w -DPLANET_HOST -DSHAPOGFX_FORMAT_GRAY1=0 -DSHAPOGFX_FORMAT_RGB444=0 -DSHAPOGFX_FORMAT_ARGB4444=0 \
//       -DSHAPOGFX_FORMAT_RGB565_SWAPPED=1 -DSHAPOGFX2D_TRANSFORM=0 -DSHAPOGFX2D_COLOR_KEY=0 -Isrc -I$G/include \
//       tools/intro_preview.cpp src/ui/intro.cpp src/world/planet_core.cpp $G/src/gfx2d/*.cpp -o intro_preview
#include <cstdio>
#include <cstring>
#include <vector>

#include "shapoco/gfx2d/graphics2d.hpp"
#include "ui/intro.h"
#include "world/planet_core.h"

extern "C" {
char __HeapLimit[1];
}

namespace g2 = shapoco::gfx2d;

static void frame(const char *path) {
    const int W = 320, H = 240, BH = 40;
    std::vector<uint16_t> fb((size_t)W * H, 0);
    for (int b = 0; b < H / BH; ++b) {
        uint16_t *band = fb.data() + (size_t)b * BH * W;
        std::vector<uint16_t> tmp((size_t)W * BH, 0);
        g2::Surface s = g2::makeSurface(g2::PixelFormat::RGB565_SWAPPED, W, BH, tmp.data());
        intro::band(s, b * BH, BH, 0);
        memcpy(band, tmp.data(), (size_t)W * BH * 2);
    }
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (uint16_t px : fb) {
        const uint16_t c = (uint16_t)((px >> 8) | (px << 8));
        const unsigned r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
        fputc((int)(r << 3 | r >> 2), f); fputc((int)(g << 2 | g >> 4), f); fputc((int)(b << 3 | b >> 2), f);
    }
    fclose(f);
}

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : ".";
    char p[256];
    // world select, each world
    for (int w = 0; w < intro::WORLDS; ++w) {
        intro::openWorldSelect(1000);
        planet::generate(intro::previewSeed(w), w);
        intro::input(0, 1000);  // primes the edge detector
        for (int i = 0; i < w; ++i) intro::input(1u << 2, 1000), intro::input(0, 1000);  // right, release
        intro::tick(1000);
        for (uint32_t t = 1000; t < 3500; t += 16) intro::tick(t);
        snprintf(p, sizeof p, "%s/select_%d.ppm", out, w);
        frame(p);
    }
    // cut scene of world 3 (lava)
    planet::generate(0xABCDEF12u, 3);
    intro::startCutscene(10000, 3);
    uint32_t t = 10000;
    const uint32_t shots[] = {300, 800, 1400, 2200, 3000, 3700, 4300, 4900};
    for (uint32_t ms : shots) {
        while (t < 10000 + ms) { intro::tick(t); t += 16; }
        snprintf(p, sizeof p, "%s/cut_%04u.ppm", out, ms);
        frame(p);
    }
    return 0;
}
