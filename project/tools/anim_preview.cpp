// Host preview of the animated surface colours (README Round 12): renders every world style at 4 moments of the
// animation from one CAM log line.
//   g++ -O2 -std=c++17 -DPLANET_HOST -Isrc tools/anim_preview.cpp src/world/planet_core.cpp -o anim_preview
//   grep ^CAM docs/perf/raw/cam_capture.log | sed -n 10p | ./anim_preview OUTDIR   -> OUTDIR/p<style>_t<k>.ppm
#include <cstdio>
#include <cstring>
#include <vector>
#include "world/planet_core.h"
int main(int, char **argv) {
    char line[1024]; planet::View v{}; unsigned long seed = 0;
    while (fgets(line, sizeof line, stdin)) {
        long e0, e1, e2, r16; int lvl, st; float f[12], foc, cx, cy;
        if (sscanf(line, "CAM eye %ld %ld %ld fwd %f %f %f up %f %f %f right %f %f %f focal %f c %f %f R16 %ld seed %lu lvl %d st %d",
                   &e0, &e1, &e2, &f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &f[6], &f[7], &f[8], &foc, &cx, &cy, &r16, &seed, &lvl, &st) != 19) continue;
        v.eye[0] = e0; v.eye[1] = e1; v.eye[2] = e2;
        for (int i = 0; i < 3; ++i) { v.fwd[i] = f[i]; v.up[i] = f[3 + i]; v.right[i] = f[6 + i]; }
        v.focal = foc; v.cx = cx; v.cy = cy; v.radius16 = r16; break;
    }
    std::vector<uint16_t> fb(320 * 240);
    for (int pal = 0; pal < planet::PALETTES; ++pal) {
        planet::generate((uint32_t)seed, pal);
        for (int k = 0; k < 4; ++k) {
            const uint32_t ms = 1000 + k * 450;
            while (planet::anim_step(ms)) {}           // finish a pass at this time
            planet::anim_step(ms + 25); while (planet::anim_step(ms + 25)) {}
            std::fill(fb.begin(), fb.end(), 0);
            planet::render(v, fb.data(), 320, 0, 240, 320);
            char fn[256]; snprintf(fn, sizeof fn, "%s/p%d_t%d.ppm", argv[1], pal, k);
            FILE *o = fopen(fn, "wb"); fprintf(o, "P6 320 240 255\n");
            for (uint16_t px : fb) { uint16_t c = (uint16_t)((px >> 8) | (px << 8));
                unsigned char rgb[3] = {(unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2), (unsigned char)((c & 31) << 3)}; fwrite(rgb, 1, 3, o); }
            fclose(o);
        }
    }
}
