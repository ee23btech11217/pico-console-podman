#pragma once

// Procedural world surface ("texture") for the Devour Sphere backdrop.
//
// The game draws a world only as a wireframe. This module paints the surface behind it: each 2x2
// pixel block casts a ray from the camera at the world sphere, looks up a per-world height map
// (a 6 x 64 x 64 cube map of 8-bit heights generated from the world's seed) with bilinear
// filtering, adds a fine tiling detail layer for the ground near the player, and turns the height
// into a colour through a per-world palette, with sun lighting and distance fade. Platform
// independent (the host preview tool builds it with -DPLANET_HOST).

#include <cstdint>

namespace planet {

struct View {
    int32_t eye[3];             // sphere centre -> eye, 1/16 world units
    float right[3], up[3], fwd[3];  // unit view basis
    float focal, cx, cy;        // pixels
    int32_t radius16;           // sphere radius, 1/16 world units
};

constexpr int FACE_N = 64;      // cube map face size (texels)
constexpr int DETAIL_N = 64;    // detail tile size (texels), two tiles: hills and grain
constexpr int SHADES = 32;      // brightness steps in the palette table

// Builds the maps and the palette for a world, blocking (about 90 ms on the Pico 2 at 400 MHz).
void generate(uint32_t seed, int style = -1);

// The same in small slices (a few ms each) so another core, or idle time, can build the next world
// while render() keeps painting the current one:
//   gen_begin(seed);  while (!gen_step()) {}  ...  if (gen_ready()) gen_commit();
// gen_begin/gen_step run on one thread; gen_commit swaps the new world in and must not run during render().
void gen_begin(uint32_t seed, int style = -1);  // style 0..PALETTES-1 forces the palette, -1 = from the seed
bool gen_step();    // true when the world is finished
bool gen_ready();
void gen_commit();

// Paints screen rows [y0, y0 + rows) of the frame into dst (pitchPx = pixels per row of dst, the
// first row of dst is screen row y0). width = screen width. Pixels whose ray misses the world are
// left alone. Pixel values are RGB565 byte-swapped (the LCD order of the band buffers).
void render(const View &v, uint16_t *dst, int pitchPx, int y0, int rows, int width);

// Animated surface (README Rounds 12-13), integer only: advances one short slice of the colour table, the height map
// (water / lava / pools) or the ground tiles of the current world; each starts a new pass every PLANET_ANIM_PERIOD_MS.
// Call often from an otherwise idle core (the one that also runs gen_step) with the time in ms. Returns true when it
// did work. Runs alongside render(); keeps still copies in the back world set while no world is being built.
// yield (may be null) is asked between units of work (a few us each): true = stop now, resume there next time.
bool anim_step(uint32_t ms, bool (*yield)() = nullptr);

// Distance (FU) beyond which blocks are painted 4 wide; 0 = always (cheaper, used when the world fills the screen).
void setWideFu(float fu);

// Statistics of the last render() call chain since the last takeStats(): blocks evaluated.
uint32_t takeBlocks();

// Colour of the current world's skin at a point of a flying body: (lx, ly) in the body's own units
// (x right, y forward), bodyR its radius. Same palette and terrain layers as the ground, so a body
// carries the pattern of the world it flies over. rgb = 8-bit red, green, blue.
void skin(int32_t lx, int32_t ly, int32_t bodyR, uint8_t rgb[3]);

// Unit vector towards the current world's sun (world axes), the one the ground is shaded with (Round 17: the
// flying bodies are lit by it too).
void sunDir(float out[3]);

// Which of the palettes the current world uses (0..PALETTES-1) and its name.
constexpr int PALETTES = 5;
int paletteIndex();
const char *paletteName(int i);

}  // namespace planet
