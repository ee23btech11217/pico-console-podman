#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include "world/planet_core.h"
// Renders a winged-body silhouette coloured by planet::skin() for one seed -> PPM
int main(int argc, char** argv) {
    unsigned seed = (unsigned)atol(argv[1]);
    int baseR = atoi(argv[2]), baseG = atoi(argv[3]), baseB = atoi(argv[4]);
    int mix = argc > 5 ? atoi(argv[5]) : 62;
    planet::generate(seed);
    const int W = 200, H = 200, bodyR = 1000;
    std::vector<unsigned char> img(W * H * 3, 8);
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        // wing-like silhouette: |x| shrinks with y (forward is up)
        float fx = (x - W / 2) / 90.0f, fy = (H / 2 - y) / 90.0f;
        bool in = std::fabs(fx) < (0.95f - 0.55f * (fy + 1.0f) * 0.5f) * (fy > -0.9f && fy < 0.95f) && std::fabs(fx) < 0.95f - 0.6f * std::fabs(fy) * 0.5f;
        if (fy > 0.6f && std::fabs(fx) > (1.0f - fy) * 0.9f) in = false;
        if (!in) continue;
        uint8_t w[3];
        planet::skin((int)(fx * bodyR * 1.4f), (int)(fy * bodyR * 1.4f), bodyR, w);
        int r = (w[0] * mix + baseR * (100 - mix)) / 100, g = (w[1] * mix + baseG * (100 - mix)) / 100, b = (w[2] * mix + baseB * (100 - mix)) / 100;
        r += r >> 3; g += g >> 3; b += b >> 3;
        unsigned char* p = &img[(y * W + x) * 3];
        p[0] = r > 255 ? 255 : r; p[1] = g > 255 ? 255 : g; p[2] = b > 255 ? 255 : b;
    }
    printf("P6\n%d %d\n255\n", W, H);
    fwrite(img.data(), 1, img.size(), stdout);
}
