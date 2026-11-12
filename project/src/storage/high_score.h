#pragma once

#include <cstdint>

// One best score per weapon (vulcan, laser, missile), kept in the last flash sector as three 16-byte
// checksummed records (the game's own record format). A sector written by the old single-record
// firmware has its record in slot 0 and so loads as the first weapon's score.
constexpr int HS_WEAPONS = 3;

struct HighScores {
    uint32_t score[HS_WEAPONS];
    int sphere[HS_WEAPONS];
};

// Reads the records; slots without a valid record are 0. Returns true when at least one was valid.
bool high_scores_load(HighScores &hs);

// Erases and rewrites the sector. Blocks ~50 ms and parks the other core, so call it only on a quiet
// screen (game over / title). Returns true when the read-back matches what was written.
bool high_scores_save(const HighScores &hs);
