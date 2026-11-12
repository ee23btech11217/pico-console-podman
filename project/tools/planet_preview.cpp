// Host preview of the world surface shader: renders what the game's camera would see.
//   planet_preview < CAM-lines  -> out/preview_N.ppm (one per line)
// A CAM line is what the firmware logs: "CAM eye x y z fwd .. up .. right .. focal f c cx cy R16 r seed s lvl l st n".
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "world/planet_core.h"

int main(int argc, char **argv) {
    const char *outdir = argc > 1 ? argv[1] : ".";
    char line[1024];
    int n = 0;
    uint32_t lastSeed = 0;
    bool have = false;
    while (fgets(line, sizeof line, stdin)) {
        if (strncmp(line, "CAM ", 4) != 0) continue;
        planet::View v{};
        long e0, e1, e2, r16;
        unsigned long seed;
        int lvl, st;
        float f0, f1, f2, u0, u1, u2, r0, r1, r2, foc, cx, cy;
        if (sscanf(line, "CAM eye %ld %ld %ld fwd %f %f %f up %f %f %f right %f %f %f focal %f c %f %f R16 %ld seed %lu lvl %d st %d",
                   &e0, &e1, &e2, &f0, &f1, &f2, &u0, &u1, &u2, &r0, &r1, &r2, &foc, &cx, &cy, &r16, &seed, &lvl, &st) != 19)
            continue;
        v.eye[0] = e0; v.eye[1] = e1; v.eye[2] = e2;
        v.fwd[0] = f0; v.fwd[1] = f1; v.fwd[2] = f2;
        v.up[0] = u0; v.up[1] = u1; v.up[2] = u2;
        v.right[0] = r0; v.right[1] = r1; v.right[2] = r2;
        v.focal = foc; v.cx = cx; v.cy = cy; v.radius16 = (int32_t)r16;
        if (!have || (uint32_t)seed != lastSeed) {
            auto t0 = std::chrono::steady_clock::now();
            planet::generate((uint32_t)seed);
            auto t1 = std::chrono::steady_clock::now();
            fprintf(stderr, "seed %lu palette %s (generate %.1f ms host)\n", seed, planet::paletteName(planet::paletteIndex()),
                    std::chrono::duration<double, std::milli>(t1 - t0).count());
            lastSeed = (uint32_t)seed;
            have = true;
        }
        const int W = (int)(cx * 2), H = (int)(cy * 2);
        std::vector<uint16_t> fb((size_t)W * H, 0);
        planet::render(v, fb.data(), W, 0, H, W);
        char name[256];
        snprintf(name, sizeof name, "%s/preview_%02d_lvl%d_st%d.ppm", outdir, n++, lvl, st);
        FILE *f = fopen(name, "wb");
        fprintf(f, "P6\n%d %d\n255\n", W, H);
        for (uint16_t px : fb) {
            const uint16_t s = (uint16_t)((px >> 8) | (px << 8));  // unswap
            const unsigned r = (s >> 11) & 31, g = (s >> 5) & 63, b = s & 31;
            fputc((int)(r << 3 | r >> 2), f); fputc((int)(g << 2 | g >> 4), f); fputc((int)(b << 3 | b >> 2), f);
        }
        fclose(f);
        fprintf(stderr, "%s blocks %u\n", name, planet::takeBlocks());
    }
    return 0;
}
