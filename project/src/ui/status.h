#pragma once

#include <cstdint>

#include "shapoco/gfx2d/graphics2d.hpp"

// Small messages drawn over the game's own HUD (after renderBand): whether the high score was stored when the
// game is over, and the score bonus that the upgrades and spare cores earn while playing.
struct StatusInfo {
    int state;        // sim::GameState as an int (0 title, 1 weapon select, 2 playing, 3 launch, 4 dead, 5 arrive)
    bool paused;
    int saveResult;   // 0 nothing to report, 1 stored and verified, 2 the flash write failed
    bool newRecord;   // the run just ended with a new best for its weapon
    int cores;
};

void status_draw(const shapoco::gfx2d::Surface &band, int bandY, const StatusInfo &s);
