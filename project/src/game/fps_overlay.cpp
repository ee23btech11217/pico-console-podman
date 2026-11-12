#include "game/fps_overlay.h"

#include "shapoco/gfx2d/fonts.hpp"

namespace g2 = shapoco::gfx2d;

namespace {

constexpr int MARGIN = 2;
constexpr int PAD = 2;
constexpr int LINE_H = 9;

char *put_str(char *p, char *end, const char *s) {
    while (*s && p < end) *p++ = *s++;
    return p;
}

char *put_uint(char *p, char *end, uint32_t v) {
    char tmp[12];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < (int)sizeof(tmp));
    while (n > 0 && p < end) *p++ = tmp[--n];
    return p;
}

}

void FpsCounter::frame(uint64_t now_us) {
    ++frames_;
    if (windowStartUs_ == 0) windowStartUs_ = now_us;
    const uint64_t elapsed = now_us - windowStartUs_;
    if (elapsed < 500000) return;

    fps10_ = (uint32_t)((uint64_t)frames_ * 10000000ull / elapsed);
    frames_ = 0;
    windowStartUs_ = now_us;

    char *p = text_, *end = text_ + sizeof(text_) - 1;
    p = put_str(p, end, "FPS ");
    p = put_uint(p, end, fps10_ / 10);
    p = put_str(p, end, ".");
    p = put_uint(p, end, fps10_ % 10);
    *p = '\0';
}

void fps_overlay_draw(const g2::Surface &band, int band_y, const char *text, const TempBadge &temp) {
    constexpr int BOX_H = 2 * LINE_H + 2 * PAD;
    if (band_y >= MARGIN + BOX_H) return;

    g2::Graphics2D g(band);
    g.setClipRect(0, 0, band.width, band.height);
    g.setFont(&g2::ShapoSansMono_s08c07);

    const int w = g.textMetrics("FPS 00.0").width + 2 * PAD;
    const int x = band.width - w - MARGIN;
    const int y = MARGIN - band_y;

    g.fillRect(x, y, w, BOX_H, g2::makeColor(0, 0, 0, 190));
    g.setTextColor(g2::makeColor(150, 255, 170));
    g.drawString(x + PAD, y + PAD, text);

    const int ty = y + PAD + LINE_H;
    if (temp.level >= 2) {
        g.fillRect(x, ty - 1, w, LINE_H + PAD + 1, g2::makeColor(170, 0, 0));
        if (temp.blinkOn) {
            g.setTextColor(g2::makeColor(255, 255, 255));
            g.drawString(x + PAD, ty, temp.text);
        }
    } else {
        g.setTextColor(temp.level == 1 ? g2::makeColor(255, 190, 60) : g2::makeColor(150, 255, 170));
        g.drawString(x + PAD, ty, temp.text);
    }
}
