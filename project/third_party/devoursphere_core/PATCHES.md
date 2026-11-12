# Pico 2 fork of the Devour Sphere core

Copied from /opt/devour-sphere/core (include/, src/, CMakeLists.txt; MIT licensed code). /opt is not edited.
Changes, all marked "fork":

1. `renderer.hpp`: `BackdropView` + `backdropView()` (the integer backdrop camera converted to float, valid after
   `beginFrame()`), `BandHook` + `setBandHook()`.
2. `renderer.cpp`: `updateCamera()` fills `backdropView_`; `renderBand()` calls the hook right after the band is
   cleared and before `drawBackdropBand()` (stars, wireframe), so a platform can paint the world's surface as the
   bottom layer.

Nothing else differs. Without a hook the output is identical to the original.
3. `renderer.hpp` / `sphere.cpp`: `setWireAlpha()`: the wireframe lines can be blended (0..256) over what is already in the
   band instead of overwriting it; 256 (the default) is the original solid line.
   With `setWireAlpha(0)` the wireframe is not drawn at all (the segments are still built, so the adaptive mesh budget and the
   benchmark's `LIN` row are unchanged).
4. `renderer.hpp` / `renderer.cpp`: `setSkinHook()` + `putKiteQColors()`: `drawEntity()` asks the hook for a colour at each vertex of
   every fragment kite of a body drawn in full (not the core, not the far outline, not during the hit flash) and draws the kite
   with per-vertex colours (vertex-colour material). Without a hook the flat colour is used as before.
5. `renderer.hpp` / `renderer.cpp`: `setScreenHook()`: a platform screen (menu, cut scene) may paint a whole band right after the clear; the scene is then skipped for that band.
6. `game.hpp`: `debugKillPlayer()`, used only by the scripted board test to reproduce a death (same `killEntity` path as an enemy kill).
7. `hud.cpp` / `renderer.hpp`: the title no longer shows the high score; `setWeaponHighScores()` + a `BEST n  Sk` line under each weapon on the weapon select screen (only the selected one when the layout is stacked).
8. Gameplay (sim): stronger upgrades (shield 70/50/35 % damage, overdrive fire rate x1.4/1.9/2.6, thruster dash x1.4/1.9/2.6), respawn keeps 85 % size (was 75) with 4 s invincibility (was 2), spare cores are earned (+1 per `CORE_SCORE_STEP` = 10 points, +1 per sphere devoured, up to `CORES_MAX`). (A score bonus from upgrades and cores was tried and removed again at the user's request.).
10. Body lighting: `renderer.hpp` / `renderer.cpp`: `setBodyLightHook(hook, ambient, sun)`. When a hook is set and returns a
   sun direction, `buildScene()` enables the ShapoGFX environment + parallel lights around the visible-entity loop only, and
   `putKiteQ` / `putKiteQColors` / `putQuadQ` give their vertices the kite's plane normal facing the eye. Without a hook the
   output is identical to the original (dummy normals, no lights).
9. Time up ends the game: the player's wreck of a timed-out sphere leads straight to the game over screen (no spare core is spent, the sphere is not restarted); the screen reads `TIME UP / THE SPHERE WAS NOT DEVOURED`. `debugExpireTime()` is test-only.
