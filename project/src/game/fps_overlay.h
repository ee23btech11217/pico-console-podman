#pragma once

#include <cstdint>

#include "shapoco/gfx2d/graphics2d.hpp"

// Two rows at the top right: FPS, and below it the core temperature (board/core_temp.h). The HUD starts below.
constexpr int FPS_OVERLAY_RESERVED_H = 25;

// Colour of the temperature row: 0 unknown/OK (green), 1 WARM (amber), 2 HOT (red box, blinking text), as
// CoreTempLevel minus one, clamped.
struct TempBadge {
    const char *text;  // 8 characters, e.g. "CORE 64C"
    int level;         // 0 ok, 1 warm, 2 hot
    bool blinkOn;      // HOT: text visible this half second
};

class FpsCounter {
public:

    void frame(uint64_t now_us);
    const char *text() const { return text_; }
    uint32_t fps10() const { return fps10_; }

private:
    uint64_t windowStartUs_ = 0;
    uint32_t frames_ = 0;
    uint32_t fps10_ = 0;
    char text_[16] = "FPS 0.0";
};

void fps_overlay_draw(const shapoco::gfx2d::Surface &band, int band_y, const char *text, const TempBadge &temp);
