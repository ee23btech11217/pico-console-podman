#include "game/game_loop.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "audio/sound.h"
#include "board/sleep.h"
#include "board_pins.h"
#include "board/clock.h"
#include "devoursphere/devoursphere.hpp"
#include "devoursphere/profile.hpp"
#include "devoursphere/render/benchmark.hpp"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "display/ili9341.h"
#include "game/fps_overlay.h"
#include "board/core_temp.h"
#include "input/buttons.h"
#include "input/joystick.h"
#include "storage/high_score.h"
#include "ui/intro.h"
#include "ui/status.h"
#include "world/planet.h"
#include "hardware/watchdog.h"
#include "board/fault.h"
#include "pico/rand.h"
#include "pico/stdlib.h"
#include "shapoco/gfx2d/fonts.hpp"

#ifndef GAME_SIM_ON_CORE1
#define GAME_SIM_ON_CORE1 1
#endif

#if GAME_SIM_ON_CORE1
#include "pico/flash.h"
#include "pico/multicore.h"
#endif

#ifndef GAME_RAM_CODE_CFG
#define GAME_RAM_CODE_CFG ""
#endif
#ifndef GAME_RAM_RODATA_CFG
#define GAME_RAM_RODATA_CFG ""
#endif
#ifndef GAME_COPY_TO_RAM_CFG
#define GAME_COPY_TO_RAM_CFG 0
#endif
#ifndef GAME_BENCH_AUTORUN
#define GAME_BENCH_AUTORUN 0
#endif
#ifndef GAME_LATE_LAUNCH
#define GAME_LATE_LAUNCH 1  // start the sim batch late in the present so its input is fresher
#endif
#ifndef GAME_BENCH_ENABLE
#define GAME_BENCH_ENABLE 0  // 1 = hold B for 3 s on the title starts the built-in benchmark
#endif
#ifndef GAME_INPUT_TEST
#define GAME_INPUT_TEST 0    // 1 = scripted title/start/pause/resume/restart check (no hands)
#endif
#ifndef GAME_PLANET_TEX
#define GAME_PLANET_TEX 1
#endif
#ifndef GAME_INTRO
#define GAME_INTRO 1
#endif
#ifndef GAME_SLEEP
#define GAME_SLEEP 1
#endif
// Sleep: hold B for SLEEP_HOLD_MS, or SLEEP_IDLE_S seconds without input on a quiet screen (title, paused, game over)
#ifndef GAME_TEMP_TEST
#define GAME_TEMP_TEST 0
#endif
#ifndef GAME_ARENA_GUARD
#define GAME_ARENA_GUARD 0
#endif
#ifndef SLEEP_HOLD_MS
#define SLEEP_HOLD_MS 2000
#endif
#ifndef SLEEP_IDLE_S
#define SLEEP_IDLE_S 0  // 0 = no automatic sleep (see README Round 8c); manual B-hold sleep only
#endif
#if GAME_SLEEP && !GAME_BENCH_AUTORUN
#define SLEEP_ON 1
#else
#define SLEEP_ON 0
#endif
// World select and cut scene need the planet shader and are off in the benchmark build (it plays the game by script)
#if GAME_INTRO && GAME_PLANET_TEX && !GAME_BENCH_AUTORUN
#define INTRO_ON 1
#else
#define INTRO_ON 0
#endif
#ifndef PLANET_WIRE_ALPHA
#define PLANET_WIRE_ALPHA 0    // 0..256: how solid the wireframe is over the painted surface (0 = no grid)
#endif
#ifndef GAME_SKIP_STATIC
#define GAME_SKIP_STATIC 0  // 1 = do not push a band that is identical to last frame's
#endif

namespace ds = devoursphere;
namespace sim = devoursphere::sim;
namespace render = devoursphere::render;
namespace g2 = shapoco::gfx2d;

namespace {

#define CHECK_SND(k) static_assert((int)sim::SoundKind::k == SND_##k, "SoundId order: " #k)
CHECK_SND(SHOT_VULCAN);
CHECK_SND(SHOT_LASER);
CHECK_SND(SHOT_MISSILE);
CHECK_SND(HIT_ENEMY);
CHECK_SND(HIT_PLAYER);
CHECK_SND(ENEMY_KILLED_SMALL);
CHECK_SND(ENEMY_KILLED_BIG);
CHECK_SND(PLAYER_KILLED);
CHECK_SND(GET_FRAGMENT);
CHECK_SND(GET_UPGRADE);
CHECK_SND(MENU_SELECT);
CHECK_SND(MENU_START);
CHECK_SND(LAUNCH);
CHECK_SND(ARRIVE);
CHECK_SND(TIME_ALARM);
CHECK_SND(DODGE);
#undef CHECK_SND
static_assert((int)sim::SoundKind::COUNT == SND_COUNT, "SoundId count");

constexpr int TICK_US = 1000000 / sim::TICK_RATE;
constexpr int MAX_CATCHUP = 4;

constexpr int BAND_H = LCD_BAND_H;
constexpr int BAND_COUNT = LCD_H / BAND_H;
static_assert(LCD_H % BAND_H == 0, "the bands must tile the screen exactly");

alignas(4) uint16_t g_bands[2][LCD_W * BAND_H];
int g_bandCur = 0;

// The renderer's scratch arena. The benchmark never needs more than ~13 KB of it (ARNK row); 96 KB was the
// original size, 64 KB leaves room for the double-buffered world textures.
alignas(16) uint8_t g_arena[64 * 1024];
sim::Game g_game;

constexpr uint32_t MUSIC_TIER_AT[3] = {30, 80, 160};
uint32_t g_growth = 0;
bool g_musicWasOn = false;

inline uint32_t growth_score(uint32_t sounds) {
    uint32_t g = 0;
    if (sounds & (1u << SND_GET_FRAGMENT)) g += 1;
    if (sounds & (1u << SND_ENEMY_KILLED_SMALL)) g += 2;
    if (sounds & (1u << SND_GET_UPGRADE)) g += 4;
    if (sounds & (1u << SND_ENEMY_KILLED_BIG)) g += 6;
    return g;
}

#if GAME_INPUT_TEST
volatile int g_testTier = -1;  // test builds: force the music tier
volatile int g_testLevel = 0;  // test builds: force the sphere level the music speed follows
#endif

inline void audio_after_tick() {
    const uint32_t sounds = g_game.sounds();
    sound_request(sounds);
    const sim::GameState st = g_game.state();
    const bool musicOn = !g_game.muted() && !g_game.benchmark() &&
                         (st == sim::GameState::PLAYING || st == sim::GameState::LAUNCH ||
                          st == sim::GameState::ARRIVE);
    if (musicOn && !g_musicWasOn) g_growth = 0;
    g_musicWasOn = musicOn;
    if (musicOn) {
        g_growth += growth_score(sounds);
        uint8_t level = 0;
        while (level < 3 && g_growth >= MUSIC_TIER_AT[level]) ++level;
#if GAME_INPUT_TEST
        if (g_testTier >= 0) level = (uint8_t)g_testTier;
#endif
        sound_music_level(level);
        {
            int lvl = g_game.sphereLevel();
#if GAME_INPUT_TEST
            if (g_testLevel > 0) lvl = g_testLevel;
#endif
            int pct = 100 + MUSIC_SPEED_PER_LEVEL_PCT * (lvl - 1);
            sound_music_speed(pct > MUSIC_SPEED_MAX_PCT ? MUSIC_SPEED_MAX_PCT : pct);
        }
    }
    sound_music(musicOn);
}
render::Renderer g_renderer;
render::Benchmark g_bench;
bool g_bootFallback = false;
uint32_t g_xferUs = 0;
FpsCounter g_fps;
uint32_t g_accUs = 0;

struct FrameTimes {
    int ticks = 0;
    uint32_t launch_us = 0;   // when the batch (and its input sample) was started
    uint32_t core1_wait_us = 0;  // core0 waiting for the sim core
    int skipped = 0;             // bands not sent because they did not change
    uint32_t sim_us = 0;
    uint32_t begin_us = 0;
    uint32_t raster_us = 0;
    uint32_t wait_us = 0;
    uint32_t cmd_us = 0;
};

// Analog axes from the stick; the digital UP/LEFT/RIGHT/DOWN bits of the mask
// are not used here (they only exist for the button mask / debugging).
sim::Input mask_to_input(ButtonMask mask) {
    uint8_t bits = 0;
    if (mask & (1u << BTN_IDX_X)) bits |= sim::Button::A;
    if (mask & (1u << BTN_IDX_Y)) bits |= sim::Button::PAUSE;
    if (mask & (1u << BTN_IDX_A)) bits |= sim::Button::A;
    if (mask & (1u << BTN_IDX_B)) bits |= sim::Button::B;
    int8_t ax = 0, ay = 0;
    joystick_axes(ax, ay);
    return sim::Input(ax, ay, bits);
}

// One simulation tick. While paused, a fresh press of A/X restarts the run
// (reset() goes back to the title screen, like the sim's own game-over restart;
// the high score is kept). Runs on whichever core owns g_game.
bool g_prevA = false;

void tick_game(const sim::Input &in) {
    const bool a = (in.buttons & sim::Button::A) != 0;
    if (g_game.paused() && a && !g_prevA) {
        g_game.reset(get_rand_32());
    }
    g_prevA = a;
    g_game.tick(in);
}

#if GAME_BENCH_AUTORUN
// Scripted input so a configuration can be measured with no hands: wait, hold B
// on the title (starts the benchmark), let it run, read the results, close
// them, start a game and let the AI play (paced, real-time FPS/latency).
enum class Auto { WAIT, HOLD_B, BENCH, RESULTS, CLOSE, TITLE, DEMO };
Auto g_auto = Auto::WAIT;
uint32_t g_autoT0 = 0;
int g_autoPhaseLog = 0;  // 0 boot, 1 bench running, 2 results, 3 demo

sim::Input autorun_input(sim::Input in) {
    const uint32_t now = time_us_32();
    if (g_autoT0 == 0) g_autoT0 = now | 1u;
    const uint32_t dt = now - g_autoT0;
    auto pulse = [&](uint32_t period_us, uint32_t on_us, uint8_t btn) {
        if (dt % period_us < on_us) in.buttons |= btn;
    };
    switch (g_auto) {
        case Auto::WAIT:
            if (dt > 1500000u) g_auto = Auto::HOLD_B;
            break;
        case Auto::HOLD_B:
            in.buttons |= sim::Button::B;
            if (g_bench.active()) { g_auto = Auto::BENCH; g_autoPhaseLog = 1; }
            break;
        case Auto::BENCH:
            if (g_bench.lineCount() > 0) { g_auto = Auto::RESULTS; g_autoT0 = now | 1u; g_autoPhaseLog = 2; }
            break;
        case Auto::RESULTS:
            if (dt > 7000000u) { g_auto = Auto::CLOSE; g_autoT0 = now | 1u; }
            break;
        case Auto::CLOSE:
            if (dt < 200000u) in.buttons |= sim::Button::B;
            else if (dt > 1500000u) { g_auto = Auto::TITLE; g_autoT0 = now | 1u; }
            break;
        case Auto::TITLE:
            g_game.debugAutoPlayer(true);
            g_auto = Auto::DEMO;
            g_autoPhaseLog = 3;
            g_autoT0 = now | 1u;
            break;
        case Auto::DEMO: {
            const sim::GameState st = g_game.state();
            const bool playing = st == sim::GameState::PLAYING || st == sim::GameState::LAUNCH ||
                                 st == sim::GameState::ARRIVE;
            if (!playing) pulse(700000u, 150000u, sim::Button::A);
            break;
        }
    }
    return in;
}
#else
constexpr int g_autoPhaseLog = 0;
#endif

#if SLEEP_ON
// 0 awake, 1 sleep wanted: the game is being paused first, 2 ready: nothing may start any more, sleep at the next safe point
volatile int g_sleepReq = 0;
uint32_t g_holdStartMs = 0, g_lastInputMs = 0, g_pauseSeqMs = 0, g_relMs = 0;
bool g_wakeGuard = false;
int g_pauseTries = 0;

void request_sleep(const char *why) {
    if (g_sleepReq) return;
    g_sleepReq = 1;
    g_pauseSeqMs = 0;
    g_pauseTries = 0;
    printf("SLEEP requested (%s)\n", why);
}
#endif

#if GAME_TEMP_TEST
// Temperature run (README Round 9f). Called from begin_frame() on core 0 while core 1 is idle (the Game is ours).
int g_ttPhase = -1;  // 0 boot, 1 quiet, 2 crowd, 3 sleep, 4 after
uint32_t g_ttLogMs = 0, g_ttFixMs = 0;
const char *const TT_NAME[] = {"boot", "quiet", "crowd", "sleep", "after"};
void tt_start(int level, uint32_t seed) {
    g_game.reset(seed);
    g_game.setBenchmark(true);  // AI games: no high-score saves
    g_game.debugAutoPlayer(true);
    g_game.debugStartSphere(level, 2);
    g_renderer.restart();
}
void temp_test_frame() {
    const uint32_t ms = time_us_32() / 1000u;
    const sim::GameState st = g_game.state();
    const bool playing = st == sim::GameState::PLAYING || st == sim::GameState::LAUNCH || st == sim::GameState::ARRIVE;
    int want = ms < 5000u ? 0 : ms < 45000u ? 1 : 2;
    if (g_ttPhase >= 2 && ms >= 125000u) want = 3;
    if (g_ttPhase == 3 && g_sleepReq == 0 && ms > 130000u) want = 4;  // the sleep is over
    if (g_ttPhase == 4) want = 4;
    if (want != g_ttPhase) {
        g_ttPhase = want;
        printf("TMP %lu ms phase-start %s\n", (unsigned long)ms, TT_NAME[want]);
        if (want == 1) tt_start(1, 1234);
        if (want == 2 || want == 4) tt_start(8, 7);
        if (want == 3) request_sleep("temp test");
    }
    if ((g_ttPhase == 1 || g_ttPhase == 2 || g_ttPhase == 4) && !playing && ms - g_ttFixMs > 1500u) {
        g_ttFixMs = ms;  // the AI died or the time ran out: keep the scene going
        tt_start(g_ttPhase == 1 ? 1 : 8, ms);
    }
    if (g_ttPhase != 3 && ms - g_ttLogMs >= 2000u) {
        g_ttLogMs = ms;
        const float c = board_die_temp_c();
        printf("TMP %lu ms phase %s temp %.1f\n", (unsigned long)ms, TT_NAME[g_ttPhase < 0 ? 0 : g_ttPhase], (double)c);
        core_temp_ring_add((uint8_t)(g_ttPhase < 0 ? 0 : g_ttPhase), c);
    }
    core_temp_ring_poll();
}
#endif

// While a platform screen (world select, cut scene) is up the simulation does not advance
volatile bool g_freeze = false;
volatile uint32_t g_introSounds = 0;  // sound bits core1 plays for the cut scene (it owns the sound ring)
#if INTRO_ON
uint32_t g_injectUntilMs = 0;         // after the world is confirmed: hold A for the sim so it leaves the title
#endif
bool g_planetDirty = false;           // the world on screen is a preview: rebuild the real one

// One simulation step: tick, effects, audio (or, while frozen, only the cut scene's sounds).
void sim_step(const sim::Input &in) {
    if (g_freeze) {
        const uint32_t sn = g_introSounds;
        if (sn) {
            g_introSounds = 0;
            sound_request(sn);
        }
        return;
    }
    tick_game(in);
    g_renderer.pollEffects(g_game);
    audio_after_tick();
}

// Sample the inputs now (forces a fresh ADC read) and hand them to the bench
// filter. Called right before a batch starts so the stick is never older than
// the work done since the loop last looked.
#if GAME_INPUT_TEST
// Scripted input (as button-mask bits, so the world select sees it too): title -> world select ->
// choose two worlds to the right -> confirm -> weapon select -> start -> cut scene -> pause, resume,
// pause, restart. Every step logs the state, so a stuck screen shows up in the log.
int g_testStep = 0;
uint32_t g_testT0 = 0;
uint16_t g_testBits = 0;
uint32_t g_testSec = 0xFFFFFFFFu;
uint32_t g_testLastKill = 0;
int g_testKills = 0;
bool g_testAuto = false;
bool g_testExpired = false;
bool g_testSlept = false;

void input_test() {
    g_testBits = 0;
    const uint32_t now = time_us_32();
    if (g_testT0 == 0) g_testT0 = now | 1u;
    const uint32_t dt = now - g_testT0;
    const sim::GameState st = g_game.state();
    const bool playing = st == sim::GameState::PLAYING;
    const int ui = (int)intro::screen();
    auto pulse = [&](uint32_t on_from, int bit) { if (dt >= on_from && dt < on_from + 150000u) g_testBits |= (uint16_t)(1u << bit); };
    auto step = [&](int n, const char *what) {
        printf("TEST step %d %s: state %d paused %d ui %d sel %d style %d\n", n, what, (int)st, (int)g_game.paused(), ui,
               intro::selection(), intro::style());
        g_testStep = n;
        g_testT0 = now | 1u;
    };
    switch (g_testStep) {
        case 0: if (dt > 1000000u && st == sim::GameState::TITLE) step(1, "title, pressing A (the world select must open)");
                break;
        case 1: pulse(0, BTN_IDX_X);
                if (dt > 500000u) { if (ui == 1) step(2, "world select open, two to the right"); else if (dt > 2000000u) step(90, "FAIL: world select did not open"); }
                break;
        case 2: pulse(0, BTN_IDX_RIGHT);
                pulse(400000u, BTN_IDX_RIGHT);
                if (dt > 1500000u) { if (intro::selection() == (intro::style() + 2) % intro::WORLDS) step(3, "selection moved by 2, confirming"); else step(91, "FAIL: selection did not move"); }
                break;
        case 3: pulse(0, BTN_IDX_X);
                if (dt > 900000u) { if (ui == 0 && st == sim::GameState::WEAPON_SELECT && intro::style() == 2) step(4, "world 2 chosen, weapon select, pressing A"); else if (dt > 2500000u) step(92, "FAIL: confirm"); }
                break;
        case 4: pulse(0, BTN_IDX_X);
                if (dt > 400000u && ui == 2) step(5, "cut scene running");
                else if (dt > 3000000u) step(93, "FAIL: no cut scene");
                break;
        case 5: if (ui == 0) step(6, "cut scene ended, standing still");
                else if (dt > 9000000u) step(94, "FAIL: cut scene stuck");
                break;
        case 6: if (dt > 12000000u && g_testTier < 0) { g_testTier = 3; printf("TEST music tier forced to 3\n"); }
                if (dt > 70000000u && !g_testSlept) { g_testSlept = true; request_sleep("test"); }
                if (dt > 20000000u && g_testLevel == 0) { g_testLevel = 8; printf("TEST sphere level forced to 8\n"); }
                if (dt > 400000000u && !g_testExpired && playing) { g_testExpired = true; printf("TEST time runs out now: cores %d state %d\n", g_game.cores(), (int)st); g_game.debugExpireTime(); }
                // play a little, then die twice in a row like a real game; log lives, upgrades, state each second
                if (st == sim::GameState::DEAD) { printf("TEST game over: spare cores left %d, time up %d, score %lu, best %lu\n", g_game.cores(), (int)g_game.timeUp(), (unsigned long)g_game.score(), (unsigned long)g_game.highScore()); step(60, "GAME OVER reached"); }
                else if (dt > 150000000u) step(61, "no game over within 150 s");
                else {
                    if (!g_testAuto && playing) { g_testAuto = true; g_game.debugAutoPlayer(true); }  // earn some points first
                    if (playing && g_game.player().alive && dt - g_testLastKill > 8000000u && g_testKills < 2 && dt > (g_testKills == 0 ? 80000000u : 6000000u)) {
                        g_testLastKill = dt;
                        ++g_testKills;
                        printf("TEST kill #%d: cores %d up %d%d%d score %lu\n", g_testKills, g_game.cores(), g_game.upgradeLevel(sim::UpgradeKind::SHIELD), g_game.upgradeLevel(sim::UpgradeKind::OVERDRIVE), g_game.upgradeLevel(sim::UpgradeKind::THRUSTER), (unsigned long)g_game.score());
                        g_game.debugKillPlayer();
                    }
                    if ((dt / 500000u) != g_testSec) { g_testSec = dt / 500000u; if (g_testKills > 0) printf("TEST life state %d alive %d cores %d up %d%d%d score %lu hi %lu inv %d t %lu.%lus\n", (int)st, (int)g_game.player().alive, g_game.cores(), g_game.upgradeLevel(sim::UpgradeKind::SHIELD), g_game.upgradeLevel(sim::UpgradeKind::OVERDRIVE), g_game.upgradeLevel(sim::UpgradeKind::THRUSTER), (unsigned long)g_game.score(), (unsigned long)g_game.highScore(), (int)g_game.player().invincible, (unsigned long)(g_testSec / 2), (unsigned long)(g_testSec % 2 * 5)); }
                }
                break;
        case 60: if (dt > 20000000u) step(7, "pressing PAUSE (after game over)");  // keep watching the game-over screen
                break;
        case 61: break;
        case 62: if (dt > 6000000u && playing) step(7, "pressing PAUSE");
                break;
        case 7: pulse(0, BTN_IDX_Y);
                if (dt > 500000u) { if (g_game.paused()) step(8, "paused ok, pressing PAUSE to resume"); else step(95, "FAIL: not paused"); }
                break;
        case 8: pulse(0, BTN_IDX_Y);
                if (dt > 500000u) { if (!g_game.paused()) step(9, "resumed ok, pressing PAUSE again"); else step(96, "FAIL: still paused"); }
                break;
        case 9: pulse(500000u, BTN_IDX_Y);
                if (dt > 1200000u) { if (g_game.paused()) step(10, "paused again, pressing A to restart"); else step(97, "FAIL: second pause"); }
                break;
        case 10: pulse(0, BTN_IDX_X);
                if (dt > 600000u) { if (st == sim::GameState::TITLE && !g_game.paused()) step(11, "restart ok (title), pressing A"); else step(98, "FAIL: restart"); }
                break;
        case 11: pulse(0, BTN_IDX_X);
                if (dt > 700000u) { if (ui == 1) step(12, "buttons work after restart (world select opens)"); else step(99, "FAIL: buttons dead after restart"); }
                break;
        default: if (dt > 3000000u) { g_testT0 = now | 1u; printf("TEST step %d final state %d ui %d\n", g_testStep, (int)st, ui); }
                break;
    }
}
#endif

sim::Input fresh_input() {
    ButtonMask raw = buttons_read(true);
#if GAME_INPUT_TEST
    input_test();
    raw |= g_testBits;
#endif
    {
        // Log every change of the button mask (diagnostics; humans press a few times a second at most).
        static ButtonMask prevRaw = 0xFFFF;
        static uint32_t lastPrint = 0;
        const uint32_t t = time_us_32();
        if (raw != prevRaw && (uint32_t)(t - lastPrint) > 20000u) {
            printf("BTN %03x -> %03x\n", (unsigned)(prevRaw == 0xFFFF ? 0 : prevRaw), (unsigned)raw);
            prevRaw = raw;
            lastPrint = t;
        }
    }
#if SLEEP_ON
    {
        const uint32_t nowMs = time_us_32() / 1000u;
        const bool bHeld = (raw & (1u << BTN_IDX_Y)) != 0;  // the console's B button (GP9)
        static ButtonMask prevAct = 0xFFFF;
        const ButtonMask act = (ButtonMask)(raw & ~(1u << BTN_IDX_B));  // GP11 reads pressed all the time
        if (prevAct != 0xFFFF && act != prevAct) g_lastInputMs = nowMs;
        prevAct = act;
        if (g_wakeGuard) {  // the press that woke the console must not also act in the game
            if (bHeld) return sim::Input();
            g_wakeGuard = false;
            g_lastInputMs = nowMs;
        }
        if (bHeld) {
            if (!g_holdStartMs) g_holdStartMs = nowMs;
            else if (nowMs - g_holdStartMs >= (uint32_t)SLEEP_HOLD_MS) request_sleep("B held");
        } else {
            g_holdStartMs = 0;
        }
        const sim::GameState gs = g_game.state();
#if SLEEP_IDLE_S > 0
        if ((gs == sim::GameState::TITLE || gs == sim::GameState::DEAD || g_game.paused()) &&
            nowMs - g_lastInputMs > (uint32_t)SLEEP_IDLE_S * 1000u) {
            request_sleep("idle");
        }
#else
        (void)gs;
#endif
        if (g_sleepReq == 1 && g_pauseSeqMs) {
            // pause the game first with a clean press (B itself may still be held down): 100 ms off, 150 ms on
            const uint32_t d = nowMs - g_pauseSeqMs;
            sim::Input pin = sim::Input();
            if (d >= 100 && d < 250) pin.buttons |= sim::Button::PAUSE;
            return pin;
        }
        g_lastInputMs = g_lastInputMs ? g_lastInputMs : nowMs;
    }
#endif
#if INTRO_ON
    {
        const uint32_t nowMs = time_us_32() / 1000u;
        static ButtonMask prevUi = 0xFFFF;
        const ButtonMask masked = (ButtonMask)(raw & ~(1u << BTN_IDX_B));  // GP11 reads pressed all the time
        const ButtonMask edge = prevUi == 0xFFFF ? 0 : (ButtonMask)(masked & ~prevUi);
        prevUi = masked;
        if (intro::active()) {
            const intro::Event ev = intro::input(raw, nowMs);
            if (ev == intro::Event::CONFIRM) {
                g_freeze = false;
                g_injectUntilMs = nowMs + 180;  // the sim sees A and leaves the title
            } else if (ev == intro::Event::CANCEL) {
                g_freeze = false;
                g_planetDirty = true;  // bring back the real world instead of the preview
            }
            return sim::Input();  // the screens own the input
        }
        if (g_game.state() == sim::GameState::TITLE && !g_game.benchmark() &&
            (edge & ((1u << BTN_IDX_X) | (1u << BTN_IDX_A)))) {
            intro::openWorldSelect(nowMs);
            g_freeze = true;
            return sim::Input();
        }
    }
#endif
    sim::Input in = mask_to_input(raw);
#if INTRO_ON
    if (time_us_32() / 1000u < g_injectUntilMs) in.buttons |= sim::Button::A;
#endif
#if GAME_BENCH_AUTORUN
    in = autorun_input(in);
#endif
#if GAME_BENCH_ENABLE || GAME_BENCH_AUTORUN
    return g_bench.input(in);
#else
    return in;
#endif
}

#if GAME_BENCH_ENABLE || GAME_BENCH_AUTORUN
#define BENCH_ON 1
#else
#define BENCH_ON 0
#endif

uint32_t profile_clock_us() { return (uint32_t)time_us_64(); }

// Between batches, before beginFrame(). Without the benchmark compiled in, a
// "hold B on the title" request is thrown away so it can never take over the game.
void bench_before_frame(const FrameTimes &now, const FrameTimes &last);

void update_profile_clock() {
    devoursphere::profileClockUs = g_bench.active() ? profile_clock_us : nullptr;
}

#if BENCH_ON
render::BenchSample make_sample(const FrameTimes &now, const FrameTimes &last) {
    render::BenchSample b;
    b.tickUs = now.sim_us;
    b.beginUs = last.begin_us;
    b.rasterUs = last.raster_us;
    b.dmaUs = last.wait_us;
    b.cmdUs = last.cmd_us;
    b.waitUs = now.core1_wait_us;
    b.xferUs = g_xferUs;
    return b;
}
#endif

void bench_before_frame(const FrameTimes &now, const FrameTimes &last) {
#if BENCH_ON
    g_bench.beforeFrame(g_game, g_renderer, make_sample(now, last));
#else
    (void)now;
    (void)last;
    (void)g_game.takeBenchmarkRequest();
#endif
}

int ticks_due() {
    int n = 0;
    while (g_accUs >= (uint32_t)TICK_US && n < MAX_CATCHUP) {
        g_accUs -= (uint32_t)TICK_US;
        ++n;
    }
    if (g_accUs >= (uint32_t)TICK_US) {
        g_accUs = TICK_US - 1;
    }
    return n;
}

g2::Surface band_surface(int idx) {
    return g2::makeSurface(g2::PixelFormat::RGB565_SWAPPED, LCD_W, BAND_H, g_bands[idx]);
}

// One best score per weapon. The Game keeps a single high score for its own HUD logic, so it is set to the
// best of the weapon in play (or being looked at on the weapon select) before it is compared.
HighScores g_hs = {};
bool g_hsDirty = false;
int g_saveResult = 0;        // 0 nothing yet this game, 1 stored and verified, 2 write failed
bool g_newRecord = false;    // this run beat the best of its weapon

// The weapon whose record is relevant now, or -1 (title: no weapon yet)
int hs_weapon() {
    switch (g_game.state()) {
        case sim::GameState::WEAPON_SELECT: return g_game.selectedWeapon();
        case sim::GameState::PLAYING:
        case sim::GameState::LAUNCH:
        case sim::GameState::ARRIVE:
        case sim::GameState::DEAD: return (int)g_game.player().weapon;
        default: return -1;
    }
}

void update_high_score() {
    const int w = hs_weapon();
    const sim::GameState st = g_game.state();
    if (st == sim::GameState::TITLE || st == sim::GameState::WEAPON_SELECT) {
        g_saveResult = 0;  // a new game: forget the last game's message
        g_newRecord = false;
    }
    if (w >= 0 && w < HS_WEAPONS) {
        g_game.setHighScore(g_hs.score[w], g_hs.sphere[w]);
        if (g_game.keepHighScore()) {
            g_hs.score[w] = g_game.highScore();
            g_hs.sphere[w] = g_game.highScoreSphere();
            g_hsDirty = true;
            g_newRecord = true;
        }
    }
    g_renderer.setWeaponHighScores(g_hs.score, g_hs.sphere);
    // Write to flash only when the run is over, so the ~50 ms erase never lands in the middle of play.
    const bool running = st == sim::GameState::PLAYING || st == sim::GameState::LAUNCH || st == sim::GameState::ARRIVE;
    if (!running && g_hsDirty) {
        g_saveResult = high_scores_save(g_hs) ? 1 : 2;
        g_hsDirty = false;  // also on failure: do not retry every frame
    }
}

#if GAME_SKIP_STATIC
uint32_t g_bandHash[BAND_COUNT];
bool g_bandHashOk[BAND_COUNT];

// FNV-style hash of a band; ~3 cycles per word, hidden under the previous band's DMA.
uint32_t __not_in_flash_func(band_hash)(const uint16_t *px) {
    const uint32_t *w = (const uint32_t *)px;
    uint32_t h = 2166136261u;
    for (int i = 0; i < LCD_W * BAND_H / 2; ++i) h = (h ^ w[i]) * 16777619u;
    return h;
}
#endif

// Called at the top of every band while a frame is presented (bands left, this
// one included). The dual-core path uses it to start the sim batch late.
void (*g_bandHook)(int bandsLeft) = nullptr;

void present_frame(FrameTimes &t) {
#if INTRO_ON
    const int g_fadeP = intro::fadeInPermille(time_us_32() / 1000u);  // the game fades in from black after the cut scene
#endif
    t.raster_us = 0;
    t.wait_us = 0;
    t.cmd_us = 0;
    t.skipped = 0;
    TempBadge badge;
    badge.text = core_temp_text();
    {
        const int lv = (int)core_temp_level() - 1;
        badge.level = lv < 0 ? 0 : lv;
        badge.blinkOn = (time_us_32() / 500000u) & 1u;
    }
    for (int b = 0; b < BAND_COUNT; ++b) {
        clock_crumb(14);
        if (g_bandHook) g_bandHook(BAND_COUNT - b);
        clock_crumb(15);
        const int y = b * BAND_H;
        const int idx = g_bandCur;

        const uint32_t t0 = (uint32_t)time_us_64();
        g_renderer.renderBand(band_surface(idx), y, BAND_H, 0);
        if (!intro::active()) {
            const StatusInfo si = {(int)g_game.state(), g_game.paused(), g_saveResult, g_newRecord, g_game.cores()};
            status_draw(band_surface(idx), y, si);
        }
#if INTRO_ON
        if (intro::screen() != intro::Screen::CUTSCENE) fps_overlay_draw(band_surface(idx), y, g_fps.text(), badge);
        if (g_fadeP > 0) intro::fadeBand(g_bands[idx], LCD_W, BAND_H, g_fadeP);
#else
        fps_overlay_draw(band_surface(idx), y, g_fps.text(), badge);
#endif
        clock_crumb(16);
        const uint32_t t1 = (uint32_t)time_us_64();
        t.raster_us += t1 - t0;

#if GAME_SKIP_STATIC
        // The panel keeps its memory: an unchanged band needs no transfer. The
        // buffer index only flips on a real push so the buffer in flight is never reused.
        const uint32_t h = band_hash(g_bands[idx]);
        const bool same = g_bandHashOk[b] && g_bandHash[b] == h;
        g_bandHash[b] = h;
        g_bandHashOk[b] = true;
        t.raster_us += (uint32_t)time_us_64() - t1;
        if (same) {
            ++t.skipped;
            continue;
        }
#endif
        g_bandCur ^= 1;

        lcd_wait_idle();
        clock_crumb(17);
        const uint32_t t2 = (uint32_t)time_us_64();
        t.wait_us += t2 - t1;

        lcd_write_band_start(y, BAND_H, g_bands[idx]);
        t.cmd_us += (uint32_t)time_us_64() - t2;
    }
}

#if GAME_PLANET_TEX
uint32_t g_planetSeed = 0;
bool g_planetReady = false;
uint32_t g_core0Critical = 0;  // 1 while core 0 runs Renderer::beginFrame (see sim_waiting)
#endif

#if INTRO_ON
uint32_t g_cutSoundMs = 0;
sim::GameState g_lastState = sim::GameState::TITLE;
#endif
int g_planetStyle = -2;

void begin_frame(FrameTimes &ft) {
#if GAME_TEMP_TEST
    temp_test_frame();
#endif
    core_temp_poll(time_us_32() / 1000u);  // on-screen core temperature, 1 read/s, non-blocking (core 0, as the stick)
#if INTRO_ON
    {
        const uint32_t nowMs = time_us_32() / 1000u;
        const sim::GameState st = g_game.state();
        // A new game: the first arrival after the weapon select starts the cut scene (the sim waits)
        if (st == sim::GameState::ARRIVE && g_lastState == sim::GameState::WEAPON_SELECT && !g_game.benchmark() &&
            !intro::active()) {
            intro::startCutscene(nowMs, intro::styleForLevel(g_game.sphereLevel()));
            g_freeze = true;
            g_introSounds |= 1u << SND_LAUNCH;
            g_cutSoundMs = nowMs;
        }
        g_lastState = st;
        const intro::Event ev = intro::tick(nowMs);
        if (ev == intro::Event::CUTSCENE_DONE) g_freeze = false;
        if (intro::screen() == intro::Screen::CUTSCENE && g_cutSoundMs && nowMs - g_cutSoundMs > 1500) {
            g_cutSoundMs = 0;
            g_introSounds |= 1u << SND_ARRIVE;  // dropping out of warp
        }
        const int pv = intro::takePreviewRequest();
        if (pv >= 0) {
#if GAME_SIM_ON_CORE1
            planet_request_world(intro::previewSeed(pv), pv);
#else
            planet_set_world(intro::previewSeed(pv), pv);
#endif
        }
    }
#endif
#if GAME_PLANET_TEX
    // A new world (sphere seed or style) needs its surface generated; the Game belongs to this core here.
    // While the world select is up, the world on screen is its preview and is left alone.
#if INTRO_ON
    const int wantStyle = intro::styleForLevel(g_game.sphereLevel());
    const bool previewing = intro::screen() == intro::Screen::WORLD_SELECT;
#else
    const int wantStyle = -1;
    const bool previewing = false;
#endif
    if (!g_planetReady) {
        g_planetSeed = g_game.sphereSeed();
        g_planetStyle = wantStyle;
        g_planetReady = true;
        planet_set_world(g_planetSeed, wantStyle);  // first world: nothing to show meanwhile
    } else if (!previewing && (g_game.sphereSeed() != g_planetSeed || wantStyle != g_planetStyle || g_planetDirty)) {
        g_planetSeed = g_game.sphereSeed();
        g_planetStyle = wantStyle;
        g_planetDirty = false;
#if GAME_SIM_ON_CORE1
        planet_request_world(g_planetSeed, wantStyle);  // built on core1 in slices; the old surface stays up
#else
        planet_set_world(g_planetSeed, wantStyle);
#endif
    }
#if GAME_SIM_ON_CORE1
    planet_commit_if_ready();
#endif
#endif
    const uint32_t t0 = (uint32_t)time_us_64();
    // Critical path: core 1 sleeps (WFE) instead of animating or spinning while this runs; SEV below wakes it
    __atomic_store_n(&g_core0Critical, 1u, __ATOMIC_RELEASE);
    g_renderer.beginFrame(g_game, g_bench.dt(ft.ticks));
    __atomic_store_n(&g_core0Critical, 0u, __ATOMIC_RELEASE);
#if GAME_SIM_ON_CORE1
    __sev();  // core 1 sleeps in WFE meanwhile (core1_main)
#endif
    ft.begin_us = (uint32_t)time_us_64() - t0;
}

#if GAME_SIM_ON_CORE1

enum class SimState : uint32_t {
    IDLE,
    RUN,
    DONE,
};

uint32_t g_simState = (uint32_t)SimState::IDLE;
int g_simWanted = 0;
sim::Input g_simIn;
int g_simRan = 0;
uint32_t g_simUs = 0;
uint32_t g_launchUs = 0;
uint32_t g_lastSimUs = 0;
bool g_launched = false;
uint64_t g_lastUs = 0;

// Fold the time since the last call into the tick accumulator.
void accumulate_time() {
    const uint64_t now = time_us_64();
    uint64_t delta = now - g_lastUs;
    g_lastUs = now;
    if (delta > (uint64_t)MAX_CATCHUP * TICK_US) delta = (uint64_t)MAX_CATCHUP * TICK_US;
    g_accUs += (uint32_t)delta;
}

SimState sim_load() {
    return (SimState)__atomic_load_n(&g_simState, __ATOMIC_ACQUIRE);
}

// Core 1's idle work (surface animation) asks this between its small units: a batch is waiting, or core 0 is in
// Renderer::beginFrame (the critical path; core 1 keeps off the memory meanwhile, README Round 13)
bool __not_in_flash_func(sim_waiting)() {
    return __atomic_load_n(&g_simState, __ATOMIC_ACQUIRE) == (uint32_t)SimState::RUN ||
           __atomic_load_n(&g_core0Critical, __ATOMIC_ACQUIRE) != 0u;
}

void sim_store(SimState s) {
    __atomic_store_n(&g_simState, (uint32_t)s, __ATOMIC_RELEASE);
}

// Start a sim batch if ticks are due. The input is sampled here, so the later this
// runs the fresher the stick/buttons are when the result reaches the screen.
bool try_launch() {
#if SLEEP_ON
    if (g_sleepReq == 2) return false;  // going to sleep: no new batch
#endif
    accumulate_time();
    const int want = g_bench.ticks(ticks_due());
    if (want <= 0) return false;
    g_simWanted = want;
    g_simIn = fresh_input();
    g_launchUs = (uint32_t)time_us_64();
    g_launched = true;
    sim_store(SimState::RUN);
    return true;
}

#if GAME_LATE_LAUNCH
// Band hook: launch once the bands still to be sent take about as long as the batch.
void late_launch_hook(int bandsLeft) {
    if (g_launched) return;
    const uint32_t need = g_lastSimUs + g_lastSimUs / 2 + 600;
    if ((uint32_t)bandsLeft * (g_xferUs / (uint32_t)BAND_COUNT) > need) return;
    try_launch();
}
#endif

#if GAME_ARENA_GUARD
// Debug: make the renderer arena read-only for core 1 (its own MPU). A stray write from the simulation or the world
// builder then traps on the spot and the fault recorder (board/fault.cpp) logs the writing instruction.
void core1_arena_readonly() {
    volatile uint32_t *const MPU_CTRL = (volatile uint32_t *)0xE000ED94;
    volatile uint32_t *const MPU_RNR = (volatile uint32_t *)0xE000ED98;
    volatile uint32_t *const MPU_RBAR = (volatile uint32_t *)0xE000ED9C;
    volatile uint32_t *const MPU_RLAR = (volatile uint32_t *)0xE000EDA0;
    volatile uint32_t *const MPU_MAIR0 = (volatile uint32_t *)0xE000EDC0;
    const uint32_t base = ((uint32_t)g_arena + 31u) & ~31u;
    const uint32_t limit = (((uint32_t)g_arena + sizeof(g_arena)) & ~31u) - 32u;  // inclusive, 32-byte granule
    *MPU_CTRL = 0;
    *MPU_MAIR0 = 0x44;                 // attribute 0: normal, non-cacheable
    *MPU_RNR = 0;
    *MPU_RBAR = (base & ~31u) | (0u << 3) | (3u << 1) | 1u;  // AP = read-only (any privilege), XN
    *MPU_RLAR = (limit & ~31u) | (0u << 1) | 1u;
    *MPU_CTRL = 1u | 4u;               // enable, privileged default map stays for everything else
    __asm volatile("dsb\n isb\n");
}
#endif

void core1_main() {
#if GAME_ARENA_GUARD
    core1_arena_readonly();
#endif
    flash_safe_execute_core_init();  // lets core0 erase/program flash safely
    for (;;) {
#if SLEEP_ON
        if (board_sleep_active()) {  // console asleep: park this core until core 0 wakes it (__sev)
            board_sleep_core1_park();
            continue;
        }
#endif
#if GAME_ARENA_GUARD == 2
        if (time_us_32() > 12000000u) ((volatile uint8_t *)g_arena)[4096] = 0x5A;  // self-test: must trap, FAULT core 1
#endif
        if (sim_load() != SimState::RUN) {
            // Core 0 in Renderer::beginFrame: no chunk job and no sim request can come meanwhile, so sleep instead of
            // spinning (no bus traffic from this core; measured: benchmark +0.1 FPS with or without the animation).
            // The event latch makes this race free: a SEV between the check and the WFE ends the WFE at once.
            if (__atomic_load_n(&g_core0Critical, __ATOMIC_ACQUIRE)) {
                __wfe();
                continue;
            }
#if GAME_PLANET_TEX
            if (planet_help_poll()) continue;  // a chunk of the band core 0 is painting (GAME_PLANET_SHARE)
            if (planet_gen_poll()) continue;   // one slice of the next world's surface
            if (planet_anim_poll(sim_waiting)) continue;  // a little of the surface animation (GAME_PLANET_ANIM)
#endif
            tight_loop_contents();
            continue;
        }
        const uint32_t t0 = (uint32_t)time_us_64();
        for (int i = 0; i < g_simWanted; ++i) {
            sim_step(g_simIn);
        }
        g_simUs = (uint32_t)time_us_64() - t0;
        g_simRan = g_simWanted;
        sim_store(SimState::DONE);
    }
}

#endif

}

void game_splash() {
    constexpr const char *TEXT = "Simple by Prajwal and Srikar";
    for (int b = 0; b < BAND_COUNT; ++b) {
        const int idx = g_bandCur;
        g_bandCur ^= 1;
        memset(g_bands[idx], 0, sizeof(g_bands[idx]));
        const g2::Surface surf = band_surface(idx);
        g2::Graphics2D g(surf);
        g.setClipRect(0, 0, LCD_W, BAND_H);
        g.setFont(&g2::ShapoSansMono_s08c07);
        g.setTextColor(g2::makeColor(255, 255, 255));
        g.drawString((LCD_W - g.textMetrics(TEXT).width) / 2, LCD_H / 2 - 4 - b * BAND_H, TEXT);
        lcd_wait_idle();
        lcd_write_band_start(b * BAND_H, BAND_H, g_bands[idx]);
    }
    lcd_wait_idle();
    constexpr uint32_t RESET_BTN = 1u << BTN_IDX_Y;
    bool armed = !(buttons_read() & RESET_BTN);
    const uint32_t t0 = time_us_32();
    while (time_us_32() - t0 < 2000000u) {
        if (!(buttons_read() & RESET_BTN)) armed = true;
        else if (armed) {
            watchdog_reboot(0, 0, 0);
            for (;;) tight_loop_contents();
        }
        clock_guard_feed();
        sleep_ms(1);
    }
}

void game_set_boot_info(bool fallback) {
    g_bootFallback = fallback;
    // After a watchdog fallback the clock is the stock 150 MHz: use a conservative SPI rate too.
    if (fallback && lcd_spi_hz() > 40000000u) lcd_set_spi_hz(40000000u);
}

// Time one full-screen transfer with nothing else running (the SPI floor).
static uint32_t measure_transfer() {
    lcd_wait_idle();
    const uint32_t t0 = (uint32_t)time_us_64();
    for (int b = 0; b < BAND_COUNT; ++b) {
        lcd_write_band_start(b * BAND_H, BAND_H, g_bands[0]);
        lcd_wait_idle();
    }
    return (uint32_t)time_us_64() - t0;
}

void game_init() {
    g_game.reset(get_rand_32());
    high_scores_load(g_hs);
    g_renderer.init(LCD_W, LCD_H, g_arena, sizeof(g_arena));

    render::ControlHints hints;
    // The controls of this console (the two buttons are called A and B): no PC key names anywhere
    hints.move = "STICK: MOVE   A: FIRE   B: PAUSE";
    hints.moveAlt = "STICK MOVE  A FIRE  B PAUSE";
    hints.dash = "UP: DASH   DOWN: BRAKE";
    hints.dashAlt = "UP DASH  DOWN BRAKE";
    hints.pause = "B: RESUME   A: RESTART   DOWN: SOUND";
    hints.pauseAlt = "B: RESUME  A: RESTART";
    g_renderer.setControlHints(hints);
    g_renderer.setHudInsets(render::HudInsets{0, 0, FPS_OVERLAY_RESERVED_H, 0});
    g_bench.setPlatform("PICO2", nullptr);
#if INTRO_ON
    g_renderer.setScreenHook(intro::band);
#endif
#if GAME_PLANET_TEX
    planet_install(g_renderer);
    g_renderer.setWireAlpha(PLANET_WIRE_ALPHA);
#endif
    memset(g_bands[0], 0, sizeof(g_bands[0]));
    g_xferUs = measure_transfer();
}

#if SLEEP_ON
// Called once per loop pass from core0 with core1 idle. Phase 1: make the game quiet (paused) first; phase 2: wait for
// the B button to be released (the wake is its next press), then sleep. Returns after the wake.
void sleep_service(bool coreIdle) {
    if (g_sleepReq == 0) return;
    const uint32_t nowMs = time_us_32() / 1000u;
    const sim::GameState gs = g_game.state();
    const bool quiet = !(gs == sim::GameState::PLAYING || gs == sim::GameState::LAUNCH || gs == sim::GameState::ARRIVE) ||
                       g_game.paused() || intro::active();
    if (g_sleepReq == 1) {
        if (quiet) {
            g_sleepReq = 2;
            g_relMs = 0;
        } else if (!g_pauseSeqMs || (nowMs - g_pauseSeqMs > 900 && g_pauseTries < 3)) {
            g_pauseSeqMs = nowMs;
            ++g_pauseTries;
        }
        return;
    }
    // phase 2: only with core1 idle (try_launch() no longer starts anything)
    if (!coreIdle) return;
    // No frames are drawn while we wait for B to be released, so the frame-fed watchdog must be fed here: holding B
    // a few seconds past the 2 s trigger used to reset the board (watchdog, crumb 13) instead of sleeping.
    // Only fed with core1 idle, so a real hang still resets.
    clock_guard_feed();
    if (!gpio_get(BTN_PIN_Y)) {  // B still down (active low)
        g_relMs = 0;
        return;
    }
    if (!g_relMs) g_relMs = nowMs;
    if (nowMs - g_relMs < 40) return;  // released for 40 ms
    printf("SLEEP enter: tick %lu state %d paused %d cores %d score %lu music %d\n", (unsigned long)g_game.tickCount(), (int)gs,
           (int)g_game.paused(), g_game.cores(), (unsigned long)g_game.score(), 0);
    board_sleep_until_pressed(BTN_PIN_Y);
    printf("WAKE: tick %lu state %d paused %d cores %d score %lu\n", (unsigned long)g_game.tickCount(),
           (int)g_game.state(), (int)g_game.paused(), g_game.cores(), (unsigned long)g_game.score());
    g_wakeGuard = true;
    g_holdStartMs = 0;
    g_lastInputMs = time_us_32() / 1000u;
    g_sleepReq = 0;
}
#endif

void game_run() {
    const uint64_t start_us = time_us_64();
    g_accUs = TICK_US;
#if GAME_SIM_ON_CORE1
    g_lastUs = start_us;
#endif
    uint64_t last_us = start_us;

    uint64_t report_us = last_us;
    uint32_t report_frames = 0;
    uint64_t sum_ticks = 0, sum_sim = 0, sum_begin = 0, sum_raster = 0, sum_wait = 0, sum_cmd = 0;
    uint64_t sum_lat = 0, sum_period = 0;
    uint32_t max_lat = 0, max_period = 0;
    uint64_t sum_skip = 0;
    uint64_t prev_done = 0;
    uint32_t report_n = 0;  // reports printed
    FrameTimes last_ft;

#if GAME_SIM_ON_CORE1
    multicore_launch_core1(core1_main);
    bool waiting = false;
    uint64_t wait_from = 0;
#endif

    while (true) {
        const uint64_t now = time_us_64();
#if GAME_SIM_ON_CORE1
        accumulate_time();
#else
        uint64_t delta = now - last_us;
        last_us = now;
        if (delta > (uint64_t)MAX_CATCHUP * TICK_US) {
            delta = (uint64_t)MAX_CATCHUP * TICK_US;
        }
        g_accUs += (uint32_t)delta;
#endif

        FrameTimes ft;

#if SLEEP_ON
#if GAME_SIM_ON_CORE1
        if (g_sleepReq) sleep_service(sim_load() == SimState::IDLE);
#else
        if (g_sleepReq) sleep_service(true);
#endif
#endif

#if GAME_SIM_ON_CORE1

        if (sim_load() == SimState::RUN) {
            if (!waiting) {
                waiting = true;
                wait_from = now;
            }
            clock_crumb(10);
            tight_loop_contents();
            continue;
        }

        int ran = 0;
        if (sim_load() == SimState::DONE) {
            ran = g_simRan;
            ft.sim_us = g_simUs;
            g_lastSimUs = g_simUs;
            ft.launch_us = g_launchUs;
            ft.core1_wait_us = waiting ? (uint32_t)(now - wait_from) : 0;
            waiting = false;
            sim_store(SimState::IDLE);
        }

        if (ran > 0) {
            ft.ticks = ran;
            update_high_score();
            bench_before_frame(ft, last_ft);
            update_profile_clock();
            clock_crumb(11);
            begin_frame(ft);
        }

        g_launched = false;
#if GAME_LATE_LAUNCH
        // With a frame to present, the batch starts later (late_launch_hook).
        if (ran == 0) {
            try_launch();
            continue;
        }
        g_bandHook = late_launch_hook;
#else
        try_launch();
        if (ran == 0) continue;
#endif
#else
        const int owed = ticks_due();
        const int ticks = g_bench.ticks(owed);
        if (ticks == 0) continue;

        const sim::Input in = fresh_input();
        const uint32_t s0 = (uint32_t)time_us_64();
        ft.launch_us = s0;
        for (int i = 0; i < ticks; ++i) {
            sim_step(in);
        }
        ft.sim_us = (uint32_t)time_us_64() - s0;
        ft.ticks = ticks;
        update_high_score();
        bench_before_frame(ft, last_ft);
        update_profile_clock();
        begin_frame(ft);
#endif

        clock_crumb(12);
        present_frame(ft);
#if GAME_SIM_ON_CORE1
        g_bandHook = nullptr;
        if (!g_launched) try_launch();
#endif
        g_renderer.endFrame();

        const uint64_t done = time_us_64();
        g_fps.frame(done);
        clock_guard_feed();
        clock_crumb(13);
        last_ft = ft;

        // Input-to-screen latency of this frame's input sample: launch of the
        // batch -> last band handed to the DMA, plus the last band's own transfer.
        const uint32_t lat = (uint32_t)done - ft.launch_us + g_xferUs / (uint32_t)BAND_COUNT;
        sum_lat += lat;
        if (lat > max_lat) max_lat = lat;
        if (prev_done) {
            const uint32_t per = (uint32_t)(done - prev_done);
            sum_period += per;
            if (per > max_period) max_period = per;
        }
        prev_done = done;

        sum_skip += (uint32_t)ft.skipped;
        sum_ticks += (uint32_t)ft.ticks;
        sum_sim += ft.sim_us;
        sum_begin += ft.begin_us;
        sum_raster += ft.raster_us;
        sum_wait += ft.wait_us;
        sum_cmd += ft.cmd_us;
        ++report_frames;
        if (done - report_us >= 1000000) {
            const uint32_t n = report_frames;
            const uint32_t tk10 = (uint32_t)(sum_ticks * 10 / n);
            int8_t axX = 0, axY = 0;
            joystick_axes(axX, axY);
            uint16_t ppx, ppy;
            uint32_t jus, jn;
            joystick_stats_take(ppx, ppy, jus, jn);
            printf("P%d FPS %lu.%lu  ticks %lu.%lu  sim %luus  begin %luus  raster %luus  wait %luus  cmd %luus"
                   "  st %d pz %d ui %d up %d%d%d core %d btn %03x  skip %lu.%lu  lat %lu/%luus  per %lu/%luus  joy %u,%u ax %d,%d jpp %u,%u jus %lu hi %lu\n",
                   g_autoPhaseLog,
                   (unsigned long)(g_fps.fps10() / 10), (unsigned long)(g_fps.fps10() % 10),
                   (unsigned long)(tk10 / 10), (unsigned long)(tk10 % 10),
                   (unsigned long)(sum_sim / n), (unsigned long)(sum_begin / n),
                   (unsigned long)(sum_raster / n), (unsigned long)(sum_wait / n),
                   (unsigned long)(sum_cmd / n), (int)g_game.state(), (int)g_game.paused(), (int)intro::screen(),
                   g_game.upgradeLevel(sim::UpgradeKind::SHIELD), g_game.upgradeLevel(sim::UpgradeKind::OVERDRIVE),
                   g_game.upgradeLevel(sim::UpgradeKind::THRUSTER), g_game.cores(), (unsigned)buttons_read(),
                   (unsigned long)(sum_skip * 10 / n / 10), (unsigned long)(sum_skip * 10 / n % 10),
                   (unsigned long)(sum_lat / n), (unsigned long)max_lat,
                   (unsigned long)(sum_period / n), (unsigned long)max_period,
                   (unsigned)joystick_raw_x(), (unsigned)joystick_raw_y(), (int)axX, (int)axY,
                   (unsigned)ppx, (unsigned)ppy, (unsigned long)jus,
                   (unsigned long)g_game.highScore());
#if GAME_PLANET_TEX
            {
                const PlanetStats ps = planet_take_stats();
                printf("PLN paint %lu us/frame  blocks %lu/frame  gen %lu us  palette %d  help %lu.%lu%%  anim %lu us/frame\n",
                       (unsigned long)(ps.paint_us / n), (unsigned long)(ps.blocks / n),
                       (unsigned long)ps.gen_us, ps.palette, (unsigned long)(ps.help_permille / 10),
                       (unsigned long)(ps.help_permille % 10), (unsigned long)(ps.anim_us / n));
            }
#endif
            if (report_n % 5 == 0) {
                char sb[160];
                sound_describe(sb, sizeof sb);
                printf("SND %s\n", sb);
#if SLEEP_ON
                board_sleep_info(sb, sizeof sb);
                printf("SLP %s\n", sb);
#endif
                {
                    const int c10 = core_temp_c10();
                    printf("TEMP core %d.%d C level %d (0 ?, 1 ok, 2 warm >= %d C, 3 hot >= %d C)\n", c10 / 10,
                           (c10 < 0 ? -c10 : c10) % 10, (int)core_temp_level(), CORE_TEMP_WARM_C10 / 10,
                           CORE_TEMP_HOT_C10 / 10);
                }
                printf("CFG sys %lu peri %lu spi %lu core1 %d tick %d band %d ramcode '%s' ramro '%s' c2r %d "
                       "qmi %08lx wdt %lx fallback %d xfer %luus share %d late %d anim %d jos %d hs %lu/%lu/%lu prevcrumb %lu s0 %08lx now %08lx all %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
                       (unsigned long)(clock_get_hz(clk_sys) / 1000),
                       (unsigned long)(clock_get_hz(clk_peri) / 1000),
                       (unsigned long)(spi_get_baudrate(spi0) / 1000), (int)GAME_SIM_ON_CORE1,
                       (int)sim::TICK_RATE, (int)BAND_H, GAME_RAM_CODE_CFG, GAME_RAM_RODATA_CFG,
                       (int)GAME_COPY_TO_RAM_CFG, (unsigned long)clock_qmi_timing(),
                       (unsigned long)clock_wdt_reason(), (int)g_bootFallback,
                       (unsigned long)g_xferUs, (int)(PLANET_SHARE ? PLANET_SHARE_ROWS : 0), (int)GAME_LATE_LAUNCH, (int)PLANET_ANIM, (int)JOY_OVERSAMPLE, (unsigned long)g_hs.score[0], (unsigned long)g_hs.score[1],
                       (unsigned long)g_hs.score[2], (unsigned long)clock_prev_crumb(), (unsigned long)clock_boot_scratch0(), (unsigned long)watchdog_hw->scratch[0], (unsigned long)clock_boot_scratch(0), (unsigned long)clock_boot_scratch(1), (unsigned long)clock_boot_scratch(2), (unsigned long)clock_boot_scratch(3), (unsigned long)clock_boot_scratch(4), (unsigned long)clock_boot_scratch(5), (unsigned long)clock_boot_scratch(6), (unsigned long)clock_boot_scratch(7));
                { char fb[220]; if (fault_snapshot(fb, sizeof fb) > 0) printf("%s\n", fb); }
            }
            if (report_n % 2 == 0) {
                const render::Renderer::BackdropView &v = g_renderer.backdropView();
                printf("CAM eye %ld %ld %ld fwd %.4f %.4f %.4f up %.4f %.4f %.4f right %.4f %.4f %.4f focal %.2f c %.1f %.1f R16 %ld seed %lu lvl %d st %d\n",
                       (long)v.eye[0], (long)v.eye[1], (long)v.eye[2], (double)v.fwd[0], (double)v.fwd[1],
                       (double)v.fwd[2], (double)v.up[0], (double)v.up[1], (double)v.up[2], (double)v.right[0],
                       (double)v.right[1], (double)v.right[2], (double)v.focal, (double)v.cx, (double)v.cy,
                       (long)v.sphereRadius16, (unsigned long)g_game.sphereSeed(), g_game.sphereLevel(),
                       (int)g_game.state());
            }
            if (g_bench.lineCount() > 0 && report_n % 3 == 0) {
                for (int i = 0; i < g_bench.lineCount(); ++i) printf("BENCH| %s\n", g_bench.line(i));
                printf("BENCH| end\n");
            }
            ++report_n;
            report_us = done;
            report_frames = 0;
            sum_ticks = sum_sim = sum_begin = sum_raster = sum_wait = sum_cmd = 0;
            sum_lat = sum_period = sum_skip = 0;
            max_lat = max_period = 0;
        }
    }
}
