#include "ui/status.h"

#include "shapoco/gfx2d/fonts.hpp"

namespace g2 = shapoco::gfx2d;

void status_draw(const g2::Surface &band, int bandY, const StatusInfo &s) {
    const char *text = nullptr;
    g2::Color col = g2::makeColor(255, 255, 255);
    int y = 0;
    if (s.state == 4) {  // game over
        y = 186;
        if (s.newRecord && s.saveResult == 1) {
            text = "NEW HIGH SCORE - SAVED";
            col = g2::makeColor(120, 255, 140);
        } else if (s.newRecord && s.saveResult == 2) {
            text = "NEW HIGH SCORE - NOT SAVED!";
            col = g2::makeColor(255, 90, 90);
        } else if (s.saveResult == 2) {
            text = "SAVE FAILED";
            col = g2::makeColor(255, 90, 90);
        }
    }
    if (!text) return;
    const int rows = band.height;
    if (y + 10 <= bandY || y >= bandY + rows) return;
    g2::Graphics2D g(band);
    g.setClipRect(0, 0, band.width, rows);
    g.setFont(&g2::ShapoSansMono_s08c07);
    const int w = g.textMetrics(text).width;
    const int x = (band.width - w) / 2;
    g.setTextColor(g2::makeColor(0, 0, 0));
    g.drawString(x + 1, y - bandY + 1, text);
    g.setTextColor(col);
    g.drawString(x, y - bandY, text);
}
