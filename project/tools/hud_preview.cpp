// Host preview of the game's own screens (title, weapon select, game over, playing) with the real renderer.
//   hud_preview <outdir>  -> hud_<name>.ppm
// Build (project root; C = third_party/devoursphere_core, G = /opt/devour-sphere/submodule/shapo-gfx):
//   g++ -O1 -std=c++17 -w -DDEVOURSPHERE_TICK_RATE=60 -DSHAPOGFX_FORMAT_GRAY1=0 -DSHAPOGFX_FORMAT_RGB444=0 -DSHAPOGFX_FORMAT_ARGB4444=0 \
//       -DSHAPOGFX_FORMAT_RGB565_SWAPPED=1 -DSHAPOGFX2D_TRANSFORM=0 -DSHAPOGFX2D_COLOR_KEY=0 -DSHAPOGFX3D_TEXTURE=0 -I$C/include -I$G/include -Isrc \
//       tools/hud_preview.cpp src/ui/status.cpp src/game/fps_overlay.cpp $C/src/sim/*.cpp $C/src/render/*.cpp $G/src/gfx2d/*.cpp $G/src/gfx3d/*.cpp -o hud_preview
#include <cstdio>
#include <cstring>
#include <vector>

#include "devoursphere/devoursphere.hpp"
#include "ui/status.h"
#include "game/fps_overlay.h"

namespace ds = devoursphere;
namespace sim = devoursphere::sim;
namespace render = devoursphere::render;
namespace g2 = shapoco::gfx2d;

alignas(16) static uint8_t arena[64 * 1024];
static sim::Game game;
static render::Renderer rend;

static StatusInfo g_status = {0, false, 0, false, 1};
static TempBadge g_badge = {"CORE 64C", 1, true};

static void shot(const char *dir, const char *name, int ticks) {
    const int W = 320, H = 240, BH = 40;
    static uint16_t band[320 * 40];
    std::vector<uint16_t> fb((size_t)W * H);
    for (int i = 0; i < ticks; ++i) { rend.pollEffects(game); }
    rend.beginFrame(game, 1.0f / 60);
    for (int b = 0; b < H / BH; ++b) {
        g2::Surface s = g2::makeSurface(g2::PixelFormat::RGB565_SWAPPED, W, BH, band);
        rend.renderBand(s, b * BH, BH, 0);
        g_status.state = (int)game.state();
        status_draw(s, b * BH, g_status);
        fps_overlay_draw(s, b * BH, "FPS 43.5", g_badge);  // as on the board (game_loop.cpp present_frame)
        memcpy(fb.data() + (size_t)b * BH * W, band, sizeof band);
    }
    rend.endFrame();
    char p[256];
    snprintf(p, sizeof p, "%s/hud_%s.ppm", dir, name);
    FILE *f = fopen(p, "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (uint16_t px : fb) {
        const uint16_t c = (uint16_t)((px >> 8) | (px << 8));
        const unsigned r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
        fputc((int)(r << 3 | r >> 2), f); fputc((int)(g << 2 | g >> 4), f); fputc((int)(b << 3 | b >> 2), f);
    }
    fclose(f);
}

static void run(int n, sim::Input in) { for (int i = 0; i < n; ++i) { game.tick(in); rend.pollEffects(game); } }

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    game.reset(12345);
    rend.init(320, 240, arena, sizeof arena);
    rend.setHudInsets(render::HudInsets{0, 0, FPS_OVERLAY_RESERVED_H, 0});
    render::ControlHints h;
    h.move = "STICK: MOVE   A: FIRE   B: PAUSE";
    h.moveAlt = "STICK MOVE  A FIRE  B PAUSE";
    h.dash = "UP: DASH   DOWN: BRAKE";
    rend.setControlHints(h);
    const uint32_t sc[3] = {120, 0, 45};
    const int sp[3] = {3, 0, 2};
    rend.setWeaponHighScores(sc, sp);
    game.setHighScore(120, 3);
    run(30, sim::Input());
    shot(dir, "title", 1);
    run(1, sim::Input(sim::Button::A));
    run(5, sim::Input());
    shot(dir, "weapon", 1);
    run(1, sim::Input(sim::Button::A));
    run(10, sim::Input());
    run(200, sim::Input());
    shot(dir, "playing", 1);
    g_badge = {"HOT! 72C", 2, true};
    shot(dir, "playing_hot", 1);
    g_badge = {"CORE 48C", 0, true};
    // die twice, as in a real game
    game.debugKillPlayer();
    run(60 * 10, sim::Input());  // wreck, respawn, 4 s of invincibility
    game.debugKillPlayer();
    run(60 * 4, sim::Input());
    g_status = {(int)game.state(), false, 1, true, 0};
    game.setHighScore(game.score(), 1);
    shot(dir, "dead_saved", 1);
    g_status = {(int)game.state(), false, 2, true, 0};
    shot(dir, "dead_failed", 1);
    // time up: a fresh game, nobody touches anything, the sphere's time runs out with spare cores left
    game.reset(777);
    run(10, sim::Input());
    run(3, sim::Input(sim::Button::A));   // title -> weapon select
    run(10, sim::Input());
    run(3, sim::Input(sim::Button::A));   // weapon select -> arrival
    run(60 * 12, sim::Input());           // arrival flight, then playing
    printf("before: state %d (2 = playing), spare cores %d\n", (int)game.state(), game.cores());
    game.debugExpireTime();
    run(60 * 2, sim::Input());
    g_status = {(int)game.state(), false, 1, true, game.cores()};
    printf("time up: state %d (4 = game over), spare cores left %d\n", (int)game.state(), game.cores());
    shot(dir, "timeup", 1);
    return 0;
}
