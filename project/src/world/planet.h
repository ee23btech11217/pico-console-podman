#pragma once

#include <cstdint>

#include "devoursphere/render/renderer.hpp"

// Paints the world surface behind the wireframe (see world/planet_core.h). Install once, then call
// planet_set_world() whenever the game's sphere seed changes (blocking, a few tens of ms).
void planet_install(devoursphere::render::Renderer &r);

// Build a world and make it current, blocking (startup, and single-core builds).
void planet_set_world(uint32_t seed, int style = -1);

// Build the next world in the background while the current one keeps being painted:
//   core0: planet_request_world(seed) when the game's sphere seed changes, planet_commit_if_ready()
//          every frame between frames (swaps a finished world in);
//   other core: planet_gen_poll() in its idle loop (one slice of a few ms per call).
void planet_request_world(uint32_t seed, int style = -1);
bool planet_gen_poll();
// Other core, idle loop, before planet_gen_poll(): paint one chunk of the band core 0 is painting right now
// (GAME_PLANET_SHARE builds; false = nothing to do). See README Round 11.
bool planet_help_poll();
// Other core, idle loop, after planet_gen_poll(): one slice of the animated surface colours (GAME_PLANET_ANIM,
// README Rounds 12-13). simWaiting() = a sim batch is waiting for this core: the animation stops between two units of
// work (a few us) when it says so or when a planet chunk job is open. Returns true when it did work.
bool planet_anim_poll(bool (*simWaiting)());
void planet_commit_if_ready();

// Since the last call: microseconds spent painting, 2x2 blocks shaded, microseconds the last world
// took from request to being current (0 if none), and the palette of the current world.
struct PlanetStats {
    uint32_t paint_us, blocks, gen_us;
    int palette;
    uint32_t help_permille;  // share of the planet chunks painted by core 1 (GAME_PLANET_SHARE)
    uint32_t anim_us;        // core 1 time in the surface animation since the last call (GAME_PLANET_ANIM)
};
PlanetStats planet_take_stats();
