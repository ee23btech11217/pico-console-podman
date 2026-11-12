# SKILLS.md — Pico 2 game on ILI9341 (SPI) + 8 buttons

Target: **RP2350 / Pico 2**, C with **official pico-sdk**, 320x240 ILI9341 over SPI, 8 GPIO buttons.
You are already inside the dev container. Work in `/workspace`.

## 0. Rules of engagement
- Update or create the workspace README before making code, build, or board-layout changes.
- **Inspect before assuming.** Run `ls` and read the README/headers of each `/opt` folder before writing code. Reuse existing drivers/assets; don't reinvent.
- Build after every meaningful change. Fix warnings; don't hide them.
- Pins live in **one file** (`board_pins.h`). If pin numbers are unknown, ask the user. Never guess wiring.
- Never edit anything under `/opt`. Copy what you need into `/workspace`.

## 1. Environment map (`/opt`)
| Folder | Use |
|---|---|
| `pico-sdk` | Official SDK. Default. `export PICO_SDK_PATH=/opt/pico-sdk` |
| `picotool` | Flash/inspect. Needs BOOTSEL mode |
| `ILI9341` | Reference/driver code for the display. Read first, adapt into the project |
| `devour-sphere` | Existing project/reference. Check for reusable game structure, assets, build setup |

## 2. Build & flash
```bash
# CMakeLists.txt must include pico_sdk_import.cmake from $PICO_SDK_PATH/external
cmake -S . -B out -DPICO_PLATFORM=rp2350 -DPICO_BOARD=pico2
cmake --build out -j2
arm-none-eabi-size -A out/game.elf
arm-none-eabi-objdump -h out/game.elf

# Flash / device checks
picotool load -fx game.uf2      # load, force, run
picotool info                   # device status
picotool info -a                # extended binary/runtime info

# Pico in BOOTSEL (hold BOOTSEL while plugging in)
```
CMake essentials: `pico_stdlib hardware_spi hardware_dma hardware_gpio` (+ `pico_multicore` if used). `pico_enable_stdio_usb(game 1)` and `pico_enable_stdio_uart(game 0)` for `printf` debugging over USB.

Current workspace build command sequence:
- `cmake -S . -B out -DPICO_PLATFORM=rp2350 -DPICO_BOARD=pico2`
- `cmake --build out -j2`
- `arm-none-eabi-size -A out/game.elf`
- `arm-none-eabi-objdump -h out/game.elf`
- `picotool load -fx game.uf2`
- `picotool info`
- `picotool info -a`

## 3. ILI9341 over SPI
- Wiring signals: SCK, MOSI, CS, DC, RST (+ BL). MISO is optional.
- **DC low = command, high = data.** Hold CS low per transaction; wait for SPI idle before toggling DC/CS.
- Init sequence: hardware reset -> `0x01` SWRESET -> `0x11` SLPOUT -> `0x3A` COLMOD `0x55` -> `0x36` MADCTL -> `0x29` DISPON.
- Window write: `0x2A` CASET -> `0x2B` RASET -> `0x2C` RAMWR.
- Pixel format **RGB565, MSB first**. Store the framebuffer byte-swapped or use DMA byte-swap.
- Start SPI at ~10-20 MHz for bring-up, then raise if wiring is short and stable.

## 4. Rendering for speed
- Full 320x240x16bpp pushes cost about 31 ms at 40 MHz, so FPS work should start by measuring and reducing pixels moved.
- Prefer dirty rectangles or bands over full-frame pushes when only a small area changes.
- If using DMA, overlap game logic with the transfer and do not reuse the buffer until DMA is done.
- Keep hot blit/fill paths in RAM with `__not_in_flash_func()` if they become a bottleneck.

## 5. Buttons (8x GPIO)
- Wire to GND, use `gpio_pull_up()` -> pressed = **low**. Define pins in an array `BTN_PINS[8]`.
- Read all buttons into one `uint8_t` bitmask each tick; derive `pressed`/`released`/`held`:
  ```c
  uint8_t now = read_buttons();            // bit i = button i down
  uint8_t pressed  = now & ~prev;          // edge down
  uint8_t released = ~now & prev;          // edge up
  prev = now;
  ```
- **Debounce by polling** at 1-2 ms (`repeating_timer` or in the main loop) and require N stable samples (~5 ms). No GPIO IRQs needed for gameplay.
- **Current hardware:** UP/LEFT/RIGHT/DOWN come from an analog joystick (VRx GP26/ADC0, VRy GP27/ADC1), see `src/input/joystick.*`; only X/Y/A/B are GPIO buttons. The joystick's SW button is **not connected and was removed** (GP28 unused); don't re-add it. Steering is **analog**: `joystick_axes()` gives -127..127 (deadzone `JOY_DEADZONE`, full scale `JOY_FULL`) and `game_loop.cpp` `mask_to_input()` passes it to `sim::Input(x, y, buttons)`; the digital direction bits are only for the button mask. Do not add per-pass blocking ADC reads; `joystick_read()` is rate-limited on purpose.
- **High score:** persisted in the last 4 KB flash sector (`src/storage/high_score.*`, sim's CRC'd `HighScoreRecord`). Loaded in `game_init()`, saved from `update_high_score()` only when not PLAYING/LAUNCH/ARRIVE and the score beat the stored one. Uses `flash_safe_execute`, so core1 must call `flash_safe_execute_core_init()` (it does). Never write flash during play, and never put firmware data in that sector. `make flash` keeps the score; `make erase` wipes it (or `picotool erase -r 0x103ff000 0x10400000` for just the score).
- **Pause restart:** while paused (Y), a fresh A/X press calls `g_game.reset()` from `tick_game()` in `game_loop.cpp` (back to title, high score kept). The pause hint text is set via `setControlHints()`. All sim ticks must go through `tick_game()`, not `g_game.tick()` directly.
- **Sound:** speaker on PWM (`AUDIO_PIN`), see `src/audio/sound.*`. Only call `sound_request()` from the simulation thread, never block in it, and keep the timer callback short (it also runs the music sequencer; no loops over data, no flash writes).
- Define an enum for roles (e.g. UP/DOWN/LEFT/RIGHT/A/B/START/SELECT) in one header; game code uses roles, never raw GPIOs.

## 6. Game loop structure
- Fixed timestep, render separately. Update by delta/fixed step, never by assuming loop speed.
- Keep game state in plain structs; prefer integer/fixed-point math where practical.
- Seed RNG from something variable, such as `get_rand_32()` or a timer.

## 7. Verification
```bash
arm-none-eabi-size -A out/game.elf            # section sizes
arm-none-eabi-objdump -h out/game.elf         # confirm sections / placement
picotool info -a                              # runtime/binary info
```
- Use USB `printf` for FPS, frame time, button bitmask.
- Logic analyzer (if available) on SCK/DC/CS to check SPI timing.
- Report to the user what you verified **on hardware** vs. only compiled.

## 8. Common pitfalls
- Wrong `PICO_PLATFORM`/`PICO_BOARD` -> builds for RP2040 by mistake. Always pass `rp2350` / `pico2`.
- Blank screen: check RST/BL pins, CS polarity, DC handling, init delays, SPI mode 0 (CPOL=0, CPHA=0).
- Garbled colours: byte order or BGR bit. Shifted image: window coords or MADCTL.
- Touching the framebuffer while DMA is still reading it -> tearing/garbage.
- Button "ghost" presses: missing pull-ups or no debounce.

## 9. SD card
Removed from the firmware (SD SPI pins are not wired). SPI0 is display-only; do not re-add FatFs or SD code unless the user wires the card and asks.

## 10. Flash / test loop from the container
- Never use `sudo` with picotool; it does not help in rootless podman. Use `make flash` (build + `picotool load -fx`), `make log` (USB printf, `LOG_SECS=n`), or `make run` for both.
- `make flash` reboots a running Pico into BOOTSEL by itself (`picotool reboot -f -u`, then polls with fresh `picotool info` processes because one long-lived picotool never sees the re-enumerated device in a rootless container), so the BOOTSEL button is only needed for the first flash or after a crash/hang with USB dead.
- USB printf appears at `/hostdev/ttyACM*` (host `/dev` mounted read-only). Output printed before the port is opened is lost, so print periodically.
- If `picotool info` says it cannot connect or `/hostdev/ttyACM*` is missing, report it to the user (host udev rule `podman/99-pico.rules` or container started with an old `run.sh`); do not try to work around it.
- Report what was verified on hardware (flash + log lines) vs. only compiled.

## 11. Verified so far
- On hardware: analog stick rests at `ax 0,0` (centre calibrated); high score flash save + reload across reboot verified with a forced test record (cleared afterwards), with core1 running.
- Pause restart: compiled and flashed (boots fine); the actual pause -> A press needs a human to confirm.
- Compiled only / needs a human: proportional steering feel under real play, and a natural game-over save. Tune `JOY_DEADZONE`/`JOY_FULL` if the stick feels dead or twitchy.

## 12. Performance setup (measured, see README "Optimisation results")
Defaults after the optimisation: 400 MHz core @ 1.30 V, whole image in RAM (`GAME_COPY_TO_RAM`), LCD SPI 66.7 MHz, 60 Hz ticks, sim on core1 with late launch. Result: ~49 FPS, ~39 ms press-to-screen (was 21-23 FPS, ~104 ms).
- **Measure with the log only**: `tools/perf_run.py NAME KEY=VALUE...` (build + flash + `make log`, ~90 s, parses the game's own benchmark and a paced AI demo into `docs/perf/results.json`), `make bench` (`make safe` was removed in Round 15; its numbers stay in the README tables). Charts: `tools/perf_plot.py` (needs matplotlib; install with `pip install --target <scratchpad>/pylib matplotlib`, not into the system). Tables: `tools/perf_table*.py`. Cmake cache options persist between builds: always pass `-DGAME_BENCH_AUTORUN=OFF` (or ON) explicitly when switching, `perf_run.py` does.
- **Never raise the LCD SPI above 66.7 MHz**: 75/80/90/100 MHz garble this panel (user checked on the LCD with `make spitest`). The log cannot tell: a timing number from a garbled picture looks fine. If you change anything SPI/clock related, build `make spitest` and ask the user to look.
- **400 MHz is the CPU limit** at 1.30 V (420-500 hung; 350 is not reachable by the PLL). 264 MHz also gives a 66 MHz SPI (divider 4) with ~8% less FPS and more margin. Do not go over 1.30 V.
- **Watchdog guard** (`clock_guard_*` in `board/clock.cpp`): fed once per completed frame; a hang in the first 5 s of frames reboots into 150 MHz/40 MHz SPI (`fallback 1` in the `CFG` log line). A re-flash within 5 s of a boot can trigger it once spuriously; reboot again. Any new long blocking loop must call `clock_guard_feed()` or it will reset the board.
- **RAM is nearly full** with `copy_to_ram` (~8 KB free). Check `arm-none-eabi-size -A out/game.elf` before adding buffers. Band height 40 is the fastest; do not enlarge the band buffers.
- **Flash**: the ROM flash routines reset the QSPI divider (sized for 150 MHz); `high_score.cpp` restores it after programming and `clock_init()` keeps the flash clock <= 100 MHz. Keep both when touching flash code.
- **Input path**: the sim batch is started late in `present_frame` through `g_bandHook` (`GAME_LATE_LAUNCH`) and samples the stick/buttons with `buttons_read(true)` at that moment. The sim must stay off the display core's critical path: `try_launch()` and the hook run on core0 only; `g_game` belongs to core1 while a batch runs.
- Things measured as useless (do not retry blindly): `-O2` sim, fixed-point gfx, 20/30-row bands, skipping unchanged bands (`GAME_SKIP_STATIC`, every band changes), 16x joystick oversampling.
- Verified on hardware (log): all numbers in README; 12 min soak at 400 MHz without reset; high-score save + reload at 400 MHz with RAM code. Verified by the user on the LCD: picture clean at SPI <= 66.7 MHz.
- **In-game benchmark is opt-in** (`GAME_BENCH_ENABLE`/`GAME_BENCH_AUTORUN`): it is started by holding B for 3 s on the title and then swallows every button for ~40 s plus the results pages, so a stuck or held B (log shows `btn 080`) used to look like a frozen game. Normal builds throw the request away.
- Log line fields `st pz btn` (game state, paused, button mask hex) are for diagnosing input problems; `GAME_INPUT_TEST=ON` is the hands-free pause/restart check.
- **Real hardware buttons** (verified with the `BTN` log, user confirmed): only GP8 (X -> sim A: choose/shoot) and GP9 (Y -> PAUSE) are wired; GP10 floats, GP11 ("B") reads pressed permanently (`btn 080`). The user calls their buttons "a" and "b" (a = choose/shoot, b = pause). Do not rely on B or A (GP10) in game logic, and never enable anything that triggers on a held B.

## 13. Round 2: music and world textures
- **World surface**: `src/world/planet_core.*` (pure C++, previewable on the PC: `g++ -DPLANET_HOST -Isrc tools/planet_preview.cpp src/world/planet_core.cpp`, feed it `CAM` lines from the log) + `src/world/planet.*` (hook, background generation). The game core is a **patched copy** in `third_party/devoursphere_core/` (see `PATCHES.md`); keep the patches minimal and listed. /opt is still untouched.
- **RAM is almost full (about 12 KB free)** with two world sets + 64 KB renderer arena. Check `arm-none-eabi-nm -n out/game.elf | grep __bss_end__` against 0x20080000 after any change. `GAME_COPY_TO_RAM` no longer fits with textures on (default is now libraries in RAM: `GAME_RAM_CODE=sim+render+gfx`, measured equal).
- **Core1 now also builds worlds** (`planet_gen_poll()` in `core1_main()`, slices of a few ms). `try_launch()` timing assumes core1 is free; a slice in progress delays a batch by up to ~4 ms. Single-core builds build worlds synchronously.
- **Wire grid is not drawn** (`PLANET_WIRE_ALPHA` 0) at the user's request; do not re-enable it unasked.
- **Music** (`audio/sound.cpp`): original epic space piece, 75..107 BPM, melody 196..587 Hz (user: "the high tones are too much", "Interstellar-like, epic"). Do not transcribe copyrighted themes; keep the register low. `tools/music_render.py` previews the tiers as WAV. Melody tables are `static_assert`ed to 8 bars of 16 steps.
- **Cache trap**: CMake options persist. A manual `make CMAKE_ARGS=...` after `perf_run.py` inherits its settings (it once built with `GAME_PLANET_TEX=OFF` and with experiment switches). Always pass `-DGAME_PLANET_TEX=... -DGAME_BENCH_AUTORUN=... -DGAME_PLANET_DEFS=` explicitly when flashing for the user.
- **Never write a file you are reading in the same expression** (`open(p,'w').write(open(p).read()...)` truncates it first; it cost `game_loop.cpp` once, rebuilt from a snapshot and the edit scripts).
- Verified on hardware (log): benchmark/real-time numbers in README round 2; the scripted pause/restart test passes; world changes under the benchmark (4 per run) cause no long frame. Verified by the user on the LCD: planet surface and brightness OK without the grid, music acceptable with the lower register ("both good").
- **Player skin** (round 3): fork patch `setSkinHook()` / `putKiteQColors()`; `world/planet.cpp::skin_hook()` mixes `planet::skin()` (the world's palette + terrain tile at the vertex position on the body) 62:38 with the player's teal. Only the player (`isPlayer`) is skinned; enemy colours (red = bigger than you, blue = smaller, gold = bounty) carry gameplay information, do not recolour them. Preview with `tools/skin_preview.cpp`. User confirmed it looks right.
- **HUD legend** (README, Round 3): bottom left cyan diamond = Shield, orange triangle = Overdrive, green chevron = Thruster (3 level bars each, dim icon = level 0); bottom right pink 4-point stars = spare cores (lives, start with 1); bottom centre = core version.

## 14. Round 4: world select and cut scene
- `src/ui/intro.*` owns both screens (painted through `Renderer::setScreenHook`, fork patch 5) and the star field. `game_loop.cpp` glue: `fresh_input()` opens the world select on a fresh A press at the title (`g_freeze` pauses the sim; sound for the cut scene goes through `g_introSounds` because core1 owns the sound ring), `begin_frame()` starts the cut scene when the state goes WEAPON_SELECT -> ARRIVE, handles preview worlds and the chosen style. `INTRO_ON` is 0 in `GAME_BENCH_AUTORUN` builds (the benchmark plays by script).
- **Free RAM is not what `sbrk(0)` says**: the SDK's optimistic sbrk gave the whole heap to malloc at the first printf. Use `mallinfo().fordblks`. The shipping build has ~24 KB free (the benchmark build ~12 KB). The star field takes all but `INTRO_RESERVE_BYTES` (2048); if you add static RAM, the star count just shrinks (it reads the free heap at run time, nothing to retune).
- The chosen world only changes the look (palette/terrain/sun); styles then advance per sphere level: `intro::styleForLevel()`. Do not offer a starting sphere level (high-score fairness).
- B (GP11) is ignored by the UI (it reads pressed permanently); confirm = X or A (the user's "a" button is X/GP8), back = Y (pause button).
- Preview the screens on the PC before flashing: `tools/intro_preview.cpp` (build line is in the README of this round / see CMake flags in the file header: needs `-DPLANET_HOST` and the shapo-gfx gfx2d sources). The scripted board test is `GAME_INPUT_TEST=ON`.
- HUD at game start: `up 000 core 1` (three empty bar outlines, one bright spare-core star) is the correct initial state; verified in the log.
- Verified on the board (log + scripted test) and by the user on the LCD: all of the above works.

## 15. Round 5: controls text, game-over crash, per-weapon high scores, rules
- **Anything run between a ROM flash call and the divider restore must be in RAM** (`high_score.cpp::write_job` is `__not_in_flash_func`). A flash fetch there crashes at 400 MHz. Test the game-over path after touching flash code: `GAME_INPUT_TEST=ON` kills the player twice and logs `HS save` and `TEST step 60`.
- UI text uses the user's button names: **A** (X/GP8: fire, confirm) and **B** (Y/GP9: pause, back). Never print firmware names or PC keys. The title hints are set in `game_init()`.
- High scores: `HighScores` in `storage/high_score.*` (3 records), `update_high_score()` sets the Game's single high score to the weapon in play (`hs_weapon()`) before `keepHighScore()`, the weapon select shows them through `Renderer::setWeaponHighScores()` (fork patch 7). The title shows none (user request). The save message is `ui/status.*`.
- Rules patch (fork patch 8): cores earned per 10 points / per sphere, stronger upgrades, gentler respawn. No score bonus and no BONUS text (removed at the user's request, do not re-add unasked). Constants in `config.hpp`.
- `tools/hud_preview.cpp` renders the real title / weapon select / playing / game over screens on the PC (needs the whole core + shapo-gfx gfx2d/gfx3d sources; command line in the file's history: see the compile line used in the README of this round), `debugKillPlayer()` (fork patch 6) is test-only.

## 16. Round 6: stored music
- Flash map: firmware from 0, **music pack at 0x80000** (`GAME_MUSIC` area, `tools/music_pack.py`, loaded by `make music`, survives `make flash`), high scores in the last sector. Post-build check: game.bin <= 512 KB.
- Audio output is a **31.25 kHz PWM DAC interrupt** (`dac_isr` in `audio/sound.cpp`, all in RAM): it plays the synth voice (effects, pulse-width tones set by the 1 kHz `sound_tick`) and, if a valid pack is found, decodes 4-bit IMA ADPCM from flash (15.625 kHz, each sample held twice). Effects cover the music; the music is decoded even while covered. No pack or a failed decoder self-test (`SND ... selftest 1` in the log) = the synthesized tune of round 2.
- Tiers -> tracks: `tier * tracks / 4`. Fades 0.6 s in/out. `MUSIC_SAMPLE_VOLUME` (70) scales the stored music only.
- The user downloaded the tracks (Pixabay blocks scripted downloads and the fetch tool cannot see audio URLs; do not try to get around the bot check). Licence: Pixabay Content License, not public domain; credits in `docs/music/CREDITS.md`.
- The user wants only a part of each track (flash footprint): `--window 75` picks the most energetic stretch.
- The interrupt costs 0.9 % of core 0 (measured with the DWT cycle counter; `SND ... isr` in the log); benchmark FPS unchanged.
- Boot breadcrumbs: `clock_crumb(n)` (watchdog scratch 1) and `prevcrumb` in the CFG log line say how far the previous boot got (8 = reached the game loop); use them if a boot hangs. Two reboots within 5 s still trip the clock guard once (documented).
- Music loudness (user: "background music is lower noise"): the pack is compressed + limited (not loudnorm) and played at the full +-127 swing around 128; the log line `SND ... music-rms n/127` is the on-board measurement (about 36-42; effects are 100-125). Do not go back to broadcast loudness normalisation for this speaker.
- Music speed follows the sphere level (user request): `sound_music_speed()`, +4 % per sphere up to 130 %, set in `audio_after_tick()`; implemented as variable-rate playback with linear interpolation in `dac_isr` (tape-style, pitch rises with tempo), slewed 8/32768 per ms. Test builds can force it with `g_testLevel`.
- **Time up = game over** (user rule, fork patch 9): `checkTransitions()` sends a timed-out player to DEAD at once (no core spent, no sphere restart); HUD says TIME UP / THE SPHERE WAS NOT DEVOURED. `restartSphereAfterTimeUp()` is now unused. Do not bring the old restart back unasked.

## 17. Round 7: sleep mode (DORMANT)
- Trigger: hold B (GP9) 2 s or 3 min idle on a quiet screen; `request_sleep()` -> phase 1 pauses the game with a synthetic PAUSE press (B may still be held, so the sim input is generated: 100 ms off, 150 ms on), phase 2 waits for B released and for core1 idle (`try_launch()` refuses while `g_sleepReq == 2`), then `board_sleep_until_pressed(BTN_PIN_Y)`. `GAME_SLEEP=OFF` removes it; it is off in benchmark builds.
- **Hard-won rules of `board/sleep.cpp`** (each cost a replug cycle): never lower the core voltage below 1.10 V (brown-out reset); wake on a **level** (edges need a clock); restart the ring oscillator (enable field only) before `runtime_init_clocks()`; interrupts must be ON before `clock_reinit_after_sleep()` (it uses `sleep_ms`); all of it is bracketed by breadcrumbs 20-33 and a 2.5 s watchdog, so a hang shows as `prevcrumb n` in the CFG log line after the reset.
- The scripted test can request a sleep (`g_testSlept`, ~110 s after boot) but only a finger on B wakes the chip, and a sleeping board cannot be flashed: use `$scratchpad/flash_when_alive.sh`-style watchers (poll the log, flash when alive) and ask the user to replug.
- The normal timer does not count while dormant (time stands still: the game resumes with no catch-up), the always-on powman timer did not count either in this state, so the sleep duration is not measured.
- Never use `pkill -f` with a pattern that appears in your own command line (it killed the shell twice).

## 18. Round 8: safe defaults
- Default is now **stock 150 MHz, SPI 37.5 MHz** (`GAME_SYS_CLOCK_KHZ=0`, `GAME_LCD_SPI_HZ=40000000`). The 400 MHz dual-core build crashed (bus fault in Graphics3D::render, see README Round 8) and the user's board overheated on USB. Do not re-enable overclock unasked; sections 12/13 numbers refer to the old 400 MHz setup.
- `board/fault.cpp` records HardFaults to .noinit RAM, printed as `FAULT` in the log. Crumbs 10-17 mark the frame loop.
- CMake cache trap bit again: a stray `GAME_SIM_ON_CORE1=OFF` made later "dual-core" tests single-core. Always pass it explicitly.
- A 3 min log run on a quiet screen triggers the idle sleep and the board vanishes from USB; press B or replug.
- 400 MHz dual-core faults every run in `Graphics3D::render`; cause not proven. 400 MHz tests heat the user's board over USB: keep each run short, flash the safe build (`-DGAME_SYS_CLOCK_KHZ=0 -DGAME_LCD_SPI_HZ=40000000 -DGAME_SIM_ON_CORE1=ON`) right after, and ask before running more.
- Default is now **200 MHz / SPI 50 MHz** (~30 FPS, 60 s clean on the log). The user must still confirm picture and board temperature.
- Round 8c: automatic idle sleep is **off** (`GAME_SLEEP_IDLE_S=0`); it is suspected of leaving the board dark, hot and deaf to B after a long run (a 5 min soak with sleep compiled out was clean). Manual hold-B sleep stays. Do not turn idle sleep back on without a long hardware soak. Figure: docs/perf/16_sleep_investigation.png (script `docs/perf/sleep_chart.py`).

## 19. Round 9: 400 MHz crash explained, default 264 MHz @ 1.15 V
- **Cause (measured on this board, figure 17)**: speed/voltage limit of the chip. Dual core: 384 MHz @1.30 V fine, 384 @1.25/1.20 V crash, 400 @1.30 V crash every run (bus fault at `pc 0x200262a6` in `Graphics3D::render`). Core 1 never wrote into the arena (MPU guard `GAME_ARENA_GUARD=1`, self-test `=2` proves the guard traps). SPI rate and flash clock ruled out.
- **Default now 264 MHz, SPI 66 MHz, 1.15 V** (`clock_init`: 1.30 V only above 300 MHz). ~43.5 FPS. Do not go back to 400 MHz; sections 12-13 numbers are from the old 400 MHz setup.
- **Sleep fix**: holding B >~5 s rebooted (frame-fed watchdog starved while waiting for B release, `wdt 1 prevcrumb 13`). `sleep_service()` now feeds it while core1 is idle. Any wait without frames must feed the watchdog.
- Flashing within 5 s of a boot still trips a spurious fallback once (seen again this round); repeat the test, don't count it.
- **Sleep is screen-only now** (`SLEEP_SCREEN_ONLY=1` in `board/sleep.cpp`): screen + speaker off, clock/voltage/USB unchanged, log `SLP alive` every 10 s. Both deep variants (DORMANT, and crystal-only light sleep with 1.10 V) failed to wake after long sleeps and made the board very hot, even on battery; do not bring them back without a 5+ minute test with the user touching the board. Figures 16/17: `docs/perf/crash_sleep_charts.py`.
- Hold-B sleep verified by the user at 264 MHz (44 s and 4.5 min sleeps, woke, cool).
- Battery start failure after Round 9 was a **loose battery connection** (user), not firmware. If "nothing at all on battery" comes back, ask about the battery wiring first.
- Figure 18 (`docs/perf/sleep_power_chart.py`) is an **estimate** of sleep power (no current sensor on the board): ~111 mA playing vs ~55 mA in screen-only sleep, about half saved, all from the backlight/LCD; the chip saves nothing (die temp 62-66 C while asleep, measured). Never present these as measured numbers.
- Round 9d: during sleep core 0 waits in WFI (B falling-edge GPIO IRQ + 1 s alarm feeding the watchdog), core 1 in WFE (`board_sleep_active()`), sound 1 kHz timer cancelled (`sound_suspend/resume`). User-verified wake. Measured: ~0.9 C cooler than cores-running sleep (figs 19/20). Fig 18's ~45 mA third bar is an estimate.
- **Round 9e sleep (shipping, user-verified wake 3x)**: screen-only sleep + cores in deep sleep (`SLEEPDEEP`, core 1 via `board_sleep_core1_park()`), `clock_sleep_slow(48000)` through the PLL, `SLEEP_EN0/1` gating of PWM/PIO/DMA/ADC/SPI/UART/I2C/HSTX/SHA/TRNG/TIMER1/JTAG/TBMAN, 1.10 V (`SLEEP_VREG`). Order: clock down, voltage down; wake: voltage up + 1 ms, clock up, then sound/LCD. Never drop below 1.10 V, never go to the bare crystal or stop the PLLs (Round 9 failures). Die temperature right after a wake is unreliable (ADC was gated). Fig 18 fourth bar (~8 mA) is an estimate.
- Tests: the user ends a sleep test by pressing B; do not run long timed logs (see memory).
- Round 9f temperature run (`GAME_TEMP_TEST=ON`, fig 21, script `docs/perf/temp_run_chart.py`): sleep readings are offset (+10 C jump at entry, -12 C at wake: not physical) but the chip still warms during sleep (+7 C in 85 s) and comes back warmer than it went in. Open question; do not claim the sleep cools the chip.
- **Die-temperature reads need GPIO23 high** (`PICO_SMPS_MODE_PIN`, regulator in PWM mode) during the read; in power-save mode (sleep load) the reading was off by up to +11 C. `die_temp_c()` in `board/sleep.cpp` does it. Corrected run: sleep 61.7 C vs play 64 C (fig 21/22). Figures 19/20 were measured without it.

### Round 9g: datasheet review of the hot sleep, recommended firmware changes (applied in Round 10, see section 20)
Source: `/workspace/RP-008373-DS-3-rp2350-datasheet.pdf` (no PDF tools in the container; text was pulled out with a small Python extractor in the scratchpad). User report: the chip/board gets too hot when the console sleeps for a long time.
- **Rule: firmware-only.** The user cannot make hardware changes: no rewiring, no added parts, no meters/INA219. Do not propose resistor/transistor/wiring fixes or external measurement gear. Heat indicators are only the die temperature (read with `PICO_SMPS_MODE_PIN` high) and the user's touch; every mA figure stays an estimate.
- **What the datasheet says**:
  - Tables 1690/1693 (§14.9.7): the whole chip is ~51-53 mW at 150 MHz (`hello_usb`), ~25 mW bus idle; peripherals are µA/MHz. At 48 MHz with both cores asleep the RP2350 dissipates a few mW, so it **cannot** heat the board by itself. A hot board in sleep means a pin is driving an external load, or heat from elsewhere on the board.
  - **RP2350-E9** (A2 stepping): a GPIO with input enabled whose pad sits between VIL and VIH leaks ~120 µA and holds itself near 2.2 V. A pull-down can't overcome it; only a drive or pull of ≤ 8.2 kΩ can. Our speaker pin GP15 is in exactly that state while asleep (input + pull-down since Round 9f). It feeds the NPN base through 1 kΩ with 10 kΩ base-to-GND (> 8.2 kΩ), so the transistor can be partly ON for the whole sleep, with the speaker on VBUS via 47 Ω. **Prime suspect for a long-sleep heat.**
  - **RP2350-E12**: with USB in use, `clk_sys` must be ≥ 10 % faster than `clk_usb` (48 MHz). Our sleep runs `clk_sys` = 48 MHz with USB up, so USB events can be lost (unreliable USB, possible interrupt storms keeping core 0 awake).
  - §6.5.2: `SLEEP_EN` gating only applies when both cores sleep **and** no DMA transfer is outstanding. The datasheet's own example gates everything except the wake source (even the bus fabric); a clock-request handshake re-enables the clocks before the CPU touches the bus. The biggest consumers we keep on: XIP 37.6 µA/MHz, IO+pads 24.5 µA/MHz (Table 1690).
  - RP2350-E4 note: `SLEEPDEEP` "brings minimal power savings over a default WFI sleep". Harmless, but don't expect much from it.
  - §6.3.1/6.3.2: the core regulator is in switching (normal) mode whenever the core is powered. Low-power linear mode (≤ 1 mA) is only reachable through POWMAN in P1 states. "RP2350 might not operate reliably with DVDD at a voltage other than 1.1 V": 1.10 V in sleep is the nominal value; never go lower.
  - Table 1692 (§14.9.7.2): only the P1.x states (switched core off, SRAM kept) reach 0.19-0.55 mW. Anything with the core powered stays in the mA range.
  - §6.5.3: stopping the crystal while a PLL still runs can send the VCO "to a very high frequency". The current sleep keeps XOSC + PLLs running, so this doesn't apply now. It is a plausible (unproven) reason the old deep variants went dark and hot.
- **Recommended changes, in this order (one build + one user sleep test each; README "Round 9g" first, per §0):**
  1. **Speaker pin LOW, low impedance, while asleep (E9)**: in `sound_suspend()` (`src/audio/sound.cpp`) replace the input + pull-down block with `gpio_init(AUDIO_PIN); gpio_set_dir(AUDIO_PIN, GPIO_OUT); gpio_put(AUDIO_PIN, 0); gpio_set_input_enabled(AUDIO_PIN, false);`. `sound_resume()` already switches it back to PWM. Fix the comment: the Round 9f "hard low kept the board warm" premise was disproven (it was the regulator reading artefact).
  2. **No undefined inputs while asleep (E9)**: a helper in `src/board/sleep.cpp`, called after `lcd_sleep()`/`sound_suspend()`, disables input enable (`gpio_set_input_enabled(pin, false)`) on every non-wake, non-driven GPIO (pins from `board_pins.h`: GP8/GP10/GP11 and the unused pins; keep GP9 = B wake pin with its pull-up; ADC pins are already input-disabled by `adc_gpio_init`). It restores everything on wake. GP11 reads "pressed" permanently, so its pull-up costs ~60 µA all the time: drop that pull-up while asleep too.
  3. **Sleep clock above 53 MHz while USB is up (E12)**: `SLEEP_SYS_KHZ` 48000 → 64000 (PLL-reachable). Optionally use a lower clock only when `stdio_usb_connected()` is false (battery).
  4. **Gate almost everything in SLEEP state (§6.5.2)**: set `SLEEP_EN0/1` to the wake minimum instead of clearing a few bits:
     - keep `CLK_SYS_IO` + `CLK_SYS_PADS` (B interrupt);
     - keep `CLK_SYS_TIMER0` + `CLK_SYS_TICKS` + `CLK_REF_TICKS` (1 s alarm) and `CLK_SYS_WATCHDOG`;
     - keep PSM/RESETS/CLOCKS/POWMAN(+ref), XOSC, PLL_SYS and PLL_USB;
     - keep `CLK_USB` + `CLK_SYS_USBCTRL` only when USB is connected;
     - gate XIP, SRAMs, bus fabric/ctrl, ROM, SIO, OTP and all other peripherals.

     Keep the save/restore of the masks. DMA is already idle (`lcd_sleep()` → `lcd_wait_idle()`). If a wake fails, step back: first keep bus fabric + SRAM0-9 on, then XIP.
  5. **Battery temperature log (diagnostic, no hardware)**: in `GAME_TEMP_TEST` builds keep the `TMP` samples in a small RAM ring (~200 × 6 B) and dump them when USB enumerates later (`stdio_usb_connected()`). The user runs quiet → crowd → sleep on battery, then plugs in. This separates board/USB/charging heat from the chip. Reuse `temp_test_frame()` (`src/game/game_loop.cpp`) and `die_temp_c()`.
  6. **Later, the only path to µA-level sleep (Table 1692)**: POWMAN P1.x (switched core off, SRAM kept, regulator automatically in low-power mode via `VREG_LP_ENTRY/EXIT`, wake via `PWRUP0` on GP9, return through the POWMAN boot vector). It is a big change: the core reboots on wake, so clocks/USB/LCD/sound must be re-initialised and the game resumed from retained SRAM. Watch RP2350-E19 (reboot hangs if `FRCE_OFF` bits are set).
- **Verify each step later**: clean build (no warnings), `arm-none-eabi-size -A out/game.elf`, `make flash` with the explicit CMake args of Round 9, short log, user sleeps/wakes with B (same tick in `SLEEP enter`/`WAKE`, no `FAULT`, `fallback 0`), user touches the board after a long sleep, and a `GAME_TEMP_TEST` run (sleep phase clearly below play, no steady rise; regenerate fig 21).

## 20. Round 10: datasheet steps applied, core temperature indicator
- **Datasheet traceability is mandatory**: every power/clock/pad/sleep/temperature change goes into `docs/datasheet/IMPLEMENTATION.md` (datasheet section, code location, HW/built status) in the same change. Datasheet text: `python3 -I tools/ds_text.py RP-008373-DS-3-rp2350-datasheet.pdf > <scratchpad>/ds.txt`, then grep. Verify claims there before coding them.
- Board is **RP2350 A2** (`chip A2` in the `SLP alive 0 s` line): RP2350-E9 applies.
- Shipping sleep = Round 9e + speaker pin GP15 driven low with IE off (step 1), IE off + button pulls off on every bank-0 pad except B/GP9 and GPIO23, restored at wake (step 2), 64 MHz sleep clock (E12, `static_assert`ed, step 3), USB clocks always on, `SLEEP_GATE_LEVEL 0` (Round 9e mask). Verified on HW: two wakes (12 s, 19 s), user "both woke, cool", die temp falls in sleep.
- **Step 4 failed**: `SLEEP_GATE_LEVEL` 3 (datasheet minimum) and 2 (+bus fabric/SRAM) never woke after sleeps > ~10 s and did not reset either (the user had to power-cycle with USB and battery off). Do not raise the level again without asking; level 1 is untested. Any try = two user-present 30 s+ sleeps, with the log running from before the sleep.
- **GPIO24 is no usable VBUS sense on this board** (reads 0 on USB, even with IE on). Do not gate USB clocks by it; gating USB with the cable in lost the link and the wake.
- Pins never `gpio_init`ed have IE off since reset: `gpio_get()` on them reads 0 (E9 note). Enable IE just for the read.
- A sleep hang leaves the board off USB and not resetting: ask the user for a full power cycle (USB + battery), and run a flash-when-alive watcher (`picotool info` or `P0` log lines) so the next build goes on at once.
- **Core temperature indicator** (`src/board/core_temp.*`, drawn by `fps_overlay_draw()`): one non-blocking read per second from `begin_frame()` on core 0 (shares the ADC with the stick); `CORE nnC` green, amber >= 60 C (WARM), red blinking `HOT! nnC` >= 70 C, 2 C hysteresis down; `TEMP` log lines. Thresholds `CORE_TEMP_WARM_C10`/`CORE_TEMP_HOT_C10`. HUD top inset is now `FPS_OVERLAY_RESERVED_H` = 25. Normal play at 264 MHz reads 58-69 C (amber); the user has not confirmed the look on the LCD yet. `core_temp_read_now()` (blocking, 2 ms) is for the sleep loop and the temperature test only.
- `GAME_TEMP_TEST` builds keep `TMP` samples in a 200-entry RAM ring, dumped as `TMPRING` lines when a USB terminal opens (battery runs). Compiled only.
- Step 6 (POWMAN P1) is still open; it needs the user's OK first (core reboots on wake).

## 21. Round 11: two-core scheduling
- Default is **dynamic work sharing of the planet surface** (`GAME_PLANET_SHARE=ON`, `GAME_PLANET_SHARE_ROWS=4`, `src/world/planet.cpp`): core 0's band hook publishes a job, both cores claim 4-row chunks with a CAS on `g_claim` = (serial << 8) | index; index 255 = closed. Core 1 claims only from its idle loop (`planet_help_poll()` before `planet_gen_poll()`), priority sim > chunks > world slices. Keep the close-before-rewrite order, or core 1 can paint a half-written job.
- Measured (README Round 11): +2.6/+7.5/+3 % FPS (quiet/crowd/flight) over the old pipeline, latency equal. The frame is now SPI-bound (DMA wait ~5 ms), so more sharing (3D raster, sim splitting) cannot gain much; the remaining 1.7 ms over the 20.5 ms floor is `begin_frame`, which needs the finished sim state.
- Late launch stays (early launch = same FPS, +9 ms latency). Pass `-DGAME_PLANET_SHARE=... -DGAME_LATE_LAUNCH=...` explicitly when flashing (cache trap). `tools/perf_run.py` defaults are now 264 MHz / 66 MHz (never 400 MHz) and include `PLANET_SHARE`.
- Core 1 helps ~36 % of the chunks (`PLN ... help n%` log field; `CFG ... share n late n`).
- User checked the LCD with sharing on: clean, no seams.
- Figure 23 `docs/perf/23_scheduling.png` (script `docs/perf/sched_chart.py`, needs matplotlib on PYTHONPATH) shows the Round 11 runs; rerun it after new `r11_*` runs.

## 22. Round 12: animated surface on core 1, whole + louder music
- **Animated surface** (`GAME_PLANET_ANIM`, ON, needs the sim on core 1): `planet::anim_step()` in `src/world/planet_core.cpp` rebuilds the shading LUT (`g_lut`) from `g_base` in 16-height slices, a pass per 20 ms, called by `planet_anim_poll()` last in core 1's idle loop. Per-world effects in `animRow()` (terra surf/shimmer, mars dust, ice glints, lava flow + embers, alien pulse). Preview on the PC: `tools/anim_preview.cpp`. Keep the slices short and the inner loop integer: the first version (float pack, 32-height slices) cost 1 FPS in the demo because a late sim launch waited behind a slice. Measured now: FPS equal to OFF, core 1 0.8-0.9 ms/frame (`PLN ... anim`). The user has not seen it on the LCD yet.
- **Music**: `make music` packs whole tracks (`MUSIC_WINDOW=0`, 3,281 KB of 3,580 KB; room for ~38 s more at most). Packer chain: 4-pole high-pass 180 Hz, +3 dB at 2 kHz, 8 dB drive into the limiter (`--hp/--presence/--drive`). More drive alone does nothing (already limited). Audible-band gain +1.8/+1.9 dB is a PC measurement (`tools/music_levels.py`); the board's `music-rms` is full band and stays ~40/34. Keep the presence lift mild (the user disliked high tones). Not confirmed by ear yet.
- Benchmark builds do not play music (`music-rms 0`); use `GAME_INPUT_TEST=ON` for an on-board music level.
- After `make music` / `make flash` the USB port can take a while to come back in the container (seen twice this round, the board was running fine: `wdt 0`, `fallback 0`). Wait and poll before calling it a hang.
- Figures 24 (memory), 25 (time utilisation, `docs/perf/time_util_chart.py [log] [phase]`), 26 (music, `docs/perf/music_r12_chart.py`); deck has 30 slides.

## 23. Round 13: height map + ground animation on core 1
- `src/world/planet_core.cpp`: jobs LUT (`animRow`), CUBE (`cubeRow`, `CUBE_ANIM[]` per palette: lim/top/amp/shore/periods/directions), LAYERS (`layerRow`, grain rows 0-63, hills 64-127). Integer only (the sine table is built once with `sinf`). The still copies live in the back world set: never touch it while `g_gen.active || g_gen.ready`; `g_commits` (incremented in `gen_commit`) triggers the re-copy. Keep this if world building changes.
- Units are tiny and `anim_step(ms, yield)` checks `yield` between units; `planet.cpp::anim_yield` = sim batch waiting (`sim_waiting` in `game_loop.cpp`) or an open chunk job or `g_core0Critical` (core 0 in `Renderer::beginFrame`). Core 1 sleeps in `__wfe()` while `g_core0Critical` is set; core 0 `__sev()`s after `beginFrame` (benchmark +0.1 FPS; the "+0.4 ms begin" seen before was demo variation, not proven contention). Do not make the units long again (cost 0.8 FPS once).
- Measure animation changes with the fixed benchmark and `GAME_PLANET_DEFS=PLANET_FORCE_STYLE=n` (A/B on one world); the AI demo differs by about +-0.5 FPS between runs. Measured: benchmark equal off/on; core 1 anim 3.0-3.4 ms/frame; core 1 busy 58 %.
- Audio interrupt at 264 MHz: 1.6 % of core 0 playing, 0.6 % silent (section 16's 0.9 % was at 400 MHz).
- `wdt 1` + random `prevcrumb` after a flash = picotool forced reboot, not a hang (all runs since Round 11).
- Figures 25 (time utilisation, now `r13_terra_on3`) and 27 (`docs/perf/anim_r13_chart.py`); strip `docs/worlds/surface_anim.png` (`tools/anim_preview.cpp`, CAM line 35). Deck: 31 slides. The user has not seen Round 13 on the LCD yet.

## 24. Round 14 (reverted at the user's request)
- Tried: boot pin probe (`PINS gnd 12,15,16,22 vdd 0 shorts 7-8,11-13` on this board) and all pads output-disabled (OD) in sleep. Die temperature in sleep unchanged (62 C), and a longer second sleep did not wake (no reset, power cycle). The user asked to go back: the board runs the Round 13 firmware (the presentation version, 283,488 B). Do not bring the probe, OD sleep or LED heartbeat back unasked.
- Facts kept: GP11 ("B") is connected to GP13 (explains `btn 080`); GP7-GP8 connected; GP12/GP22 tied to GND, GP0 to 3V3. Heat is on the chip (user). Long sleeps (> ~20 s) are untested with the Round 10-13 sleep.
- `picotool info` exits with an error ("Block loop is not valid") on this board in BOOTSEL after some builds, so `make flash`'s poll reports "not found" although the board is there: use `picotool load -x out/game.uf2` directly then.

## 25. Round 15: Makefile defaults
- `make` / `make flash` always pass `SHIP_ARGS` (the shipping firmware = the presentation version, 283,488 B, sha256 bcf83d37...), so a stale CMake cache cannot change the build; `CMAKE_ARGS` comes after and wins (`perf_run.py`, `bench`, `spitest` rely on that). Verified: a poisoned cache (150 MHz, anim off, autorun on, PLANET_FORCE_STYLE) + plain `make` gave the identical binary.
- Removed targets: `rebuild`, `reflash`, `sections` (now in `size`), `safe`. The BOOTSEL wait (`WAIT_BOOTSEL`) only treats "No accessible"/"Unable to" as absent, so the "Block loop is not valid" error no longer breaks `make flash`/`music`/`music-clear`.

## 26. Round 16: new board, ST7789 panel
- The display is now an **ST7789** (user, 2026-11-11). Landscape MADCTL `0xA0` (MY|MV, **RGB**; the ILI9341 used 0xE8 = MY|MX|MV|BGR; under MV the MX bit mirrors the logical y). Colour root cause: this module is RGB-ordered (red/blue were exchanged with BGR set). Keep `GAME_LCD_BSWAP=1` (framebuffer stored byte-swapped; 0 gave noise) and `GAME_LCD_INVERT=0` (INVON inverted the colours). All three in `SHIP_ARGS`. User confirmed the picture: "perfect".
- Pins in `board_pins.h` unchanged; the user said some connections changed, so ask before relying on button facts from section 12/24 (they describe the old board).
- Flashed and booted (log: 264 MHz, `fallback 0`). The new board had no music pack (`SND pack no tracks`, synth tune); `make music` loaded it (log `SND pack yes tracks 2 selftest 1 playing 1`). The container has no ffmpeg: pass `FFMPEG=<imageio_ffmpeg binary>` (an earlier scratchpad's `pylib/imageio_ffmpeg/binaries/`) or `pip install --target <scratchpad>/pylib imageio-ffmpeg`.
- Music pack: **synthwave only** since Round 16e (user removed the Pixabay tracks; files in `docs/music/old_pixabay/`, not packed). Plain `make music` (whole track). Synthwave source/licence unknown: ask the user.
- Music highs (user: high frequencies distort on this board): packer defaults now `--presence 0 --lp 3500` (4-pole low-pass at 3.5 kHz, README Round 16c); `--presence 3 --lp 0` = Round 12 chain. Loaded with `make music`; log `music-rms 40/127` (loudness unchanged). Not confirmed by ear yet.
- Joystick: `GAME_JOY_INVERT_UD=1` (in `SHIP_ARGS`) flips up/down (analog + digital bits) on this board (user: up/down was inverted); left/right unchanged.

## 27. Round 17: body lighting (ShapoGFX lights)
- Fork patch 10: `Renderer::setBodyLightHook(hook, ambient, sun)`; lights only around the visible-entity loop in `buildScene()`; body kites/quads get real plane normals turned to the eye (materials are double sided). Hook in `src/world/planet.cpp` uses `planet::sunDir()` (same sun as the ground).
- `GAME_BODY_LIGHT=ON`, `GAME_BODY_LIGHT_AMB=45`, `GAME_BODY_LIGHT_SUN=80` (in `SHIP_ARGS`; `perf_run.py` default `BODY_LIGHT=ON`). 60/60 looked too subtle on the PC preview.
- Measured: benchmark 44.4/41.8/45.4 -> 44.3/41.6/45.3 FPS (two runs each, identical); triangle budget unchanged. PC preview `tools/light_preview.cpp`; figures 29/30. Not yet confirmed on the LCD by the user.
