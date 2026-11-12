#pragma once

// World select screen and the start-of-game cut scene. Both are painted by our own code through the renderer's
// screen hook (the scene is skipped for the bands they paint) and both pause the simulation.

#include <cstdint>

#include "shapoco/gfx2d/graphics2d.hpp"

namespace intro {

enum class Screen : uint8_t { NONE, WORLD_SELECT, CUTSCENE };
enum class Event : uint8_t { NONE, CONFIRM, CANCEL, SKIP, CUTSCENE_DONE };

constexpr int WORLDS = 5;  // = planet::PALETTES

Screen screen();
inline bool active() { return screen() != Screen::NONE; }

// The world the player chose for the first sphere (0..WORLDS-1), and the one used for sphere `level`
// (the other worlds follow in order, so a run travels through all of them).
int style();
int selection();  // highlighted world on the select screen
int styleForLevel(int level);
const char *worldName(int style);

void openWorldSelect(uint32_t nowMs);
void startCutscene(uint32_t nowMs, int levelStyle);

// Call every frame with the raw button mask (input/buttons.h bits). Returns what the player did.
Event input(uint16_t buttonMask, uint32_t nowMs);
// Advances animations (star motion, timing). Returns CUTSCENE_DONE once when the cut scene has ended.
Event tick(uint32_t nowMs);

// World-select previews: when the selection changed this returns the style to build a preview of (else -1)
int takePreviewRequest();
uint32_t previewSeed(int style);

// 1000 right after a cut scene, falling to 0 over ~450 ms: the game scene fades in from black
int fadeInPermille(uint32_t nowMs);
void fadeBand(uint16_t *px, int pitchPx, int rows, int permille);

// The screen hook (return true: this band was painted here)
bool band(const shapoco::gfx2d::Surface &dst, int y, int h, int dstY);

// How the star field fitted into the free RAM the last time it was allocated
struct Footprint {
    uint32_t heapFree;  // bytes between the end of .bss and the top of RAM when it was allocated
    uint32_t reserve;   // left alone for the C library and stacks
    uint32_t bytes;     // bytes taken by the stars
    uint32_t stars;
};
Footprint footprint();

}  // namespace intro
