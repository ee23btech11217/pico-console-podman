cmake_minimum_required(VERSION 3.13)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED PICO_PLATFORM)
    set(PICO_PLATFORM rp2350)
endif()

if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pico2)
endif()

include(pico_sdk_import.cmake)
project(game C CXX ASM)
pico_sdk_init()
set(GAME_SYS_CLOCK_KHZ 264000 CACHE STRING "System clock in kHz (0 = stock 150 MHz). 400000 is the measured limit at 1.30 V; 264000 gives the same 66 MHz SPI with more margin")
set(GAME_LCD_SPI_HZ 66000000 CACHE STRING "Requested LCD SPI clock in Hz (this panel is clean up to 66.7 MHz, garbled at 75 MHz and above)")
set(GAME_LCD_LANDSCAPE 1 CACHE STRING "1 = landscape, 0 = portrait")
set(GAME_LCD_MADCTL "" CACHE STRING "Override ILI9341 MADCTL (0x36) value")
set(GAME_LCD_BSWAP 1 CACHE STRING "1 = DMA byte-swaps pixels (framebuffer is stored swapped; needed on both panels), 0 = no swap")
set(GAME_LCD_INVERT 0 CACHE STRING "1 = send INVON, 0 = no inversion (correct colours on this ST7789 board)")
set(GAME_TICK_RATE 60 CACHE STRING "Simulation ticks per second (30 or 60)")
set(GAME_BAND_H 40 CACHE STRING "Rows per DMA band (must divide the LCD height)")
set(GAME_RAM_CODE "sim+render+gfx" CACHE STRING "Libraries whose code runs from RAM instead of XIP flash: any of sim;render;gfx;gfx3d (empty = none)")
set(GAME_RAM_RODATA "" CACHE STRING "Libraries whose read-only data lives in RAM: any of sim;render;gfx")
option(GAME_COPY_TO_RAM "Copy the whole image to RAM at boot (SDK copy_to_ram binary type; leaves too little RAM for the planet textures)" OFF)
option(GAME_GFX_HOT "Run the ShapoGFX 3D span loops from RAM (time_critical section)" OFF)
option(GAME_GFX_FIXED "ShapoGFX 3D vertex stage in fixed point instead of float" OFF)
set(GAME_SIM_OPT "" CACHE STRING "Extra optimisation flag for the sim library, e.g. -O2 (empty = SDK default -O3)")
set(GAME_RENDER_OPT "" CACHE STRING "Extra optimisation flag for the render library")
set(GAME_GFX_OPT "" CACHE STRING "Extra optimisation flag for ShapoGFX")
set(GAME_JOY_OVERSAMPLE 4 CACHE STRING "ADC conversions averaged per joystick axis per sample")
set(GAME_JOY_INVERT_UD 1 CACHE STRING "1 = flip joystick up/down (Round 16 board), 0 = old board")
set(GAME_JOY_POLL_US 1000 CACHE STRING "Minimum microseconds between joystick ADC samples")
set(GAME_VREG_OVERRIDE 0 CACHE STRING "Raw vreg_voltage enum value to force (0 = automatic by clock)")
set(GAME_FLASH_DIV 0 CACHE STRING "QSPI flash clock divider while executing in place (0 = automatic, <=100 MHz)")
option(GAME_SPI_TEST "Build the visual SPI-rate test instead of the game" OFF)
option(GAME_SKIP_STATIC "Do not push LCD bands that are identical to the previous frame" OFF)
option(GAME_LATE_LAUNCH "Dual core: start the sim batch late in the frame so its input is fresher" ON)
option(GAME_BENCH_ENABLE "Enable the in-game benchmark (hold B for 3 s on the title). Off by default: it takes all input for ~40 s" OFF)
option(GAME_TEMP_TEST "Hands-free temperature run: quiet play, crowd, sleep (auto-wake after 90 s), TMP log lines" OFF)
option(GAME_INPUT_TEST "Scripted pause/resume/restart input test with log checks" OFF)
option(GAME_PLANET_TEX "Paint the world surface (procedural texture, lit, fogged) behind the wireframe" ON)
option(GAME_PLANET_SHARE "Dual core: core 1 helps painting the world surface (dynamic chunk sharing, README Round 11)" ON)
option(GAME_BODY_LIGHT "Light the flying bodies with the world's sun (ShapoGFX lights, README Round 17)" ON)
set(GAME_BODY_LIGHT_AMB 45 CACHE STRING "Body lighting: ambient level in percent")
set(GAME_BODY_LIGHT_SUN 80 CACHE STRING "Body lighting: sun level in percent")
option(GAME_PLANET_ANIM "Dual core: core 1 animates the world surface colours in its idle time (README Round 12)" ON)
set(GAME_PLANET_SHARE_ROWS 4 CACHE STRING "Rows per shared planet chunk (even)")
set(GAME_PLANET_DEFS "" CACHE STRING "Experiment switches for the planet shader (PLANET_NO_DETAIL, PLANET_NEAREST, ...) separated by +")
option(GAME_INTRO "World select screen and the start-of-game cut scene" ON)
set(GAME_ARENA_GUARD 0 CACHE STRING "Debug: 1 = renderer arena read-only for core 1 (MPU), a stray write traps and is logged as FAULT; 2 = same plus a deliberate write 8 s after boot (self-test)")
option(GAME_SLEEP "Sleep mode: hold B for 2 s (automatic idle sleep is off, see GAME_SLEEP_IDLE_S)" ON)
set(GAME_SLEEP_IDLE_S 0 CACHE STRING "Seconds idle on a quiet screen before automatic sleep (0 = never; it is suspected of leaving the board dead and hot)")
option(GAME_BENCH_AUTORUN "Run the built-in benchmark and a paced AI demo by itself after boot" OFF)
option(GAME_SIM_ON_CORE1 "Run the simulation on core1" ON)
option(GAME_TEXTURES "Enable ShapoGFX texture and env-map support" OFF)
set(GAME_SPHERE_PX 24 CACHE STRING "Target on-screen size in px of one sphere mesh cell (smaller = finer sphere)")
set(GAME_WIRE_MAX 1100 CACHE STRING "Size of the wireframe line array (also the in-flight line budget)")
add_compile_definitions(DEVOURSPHERE_TICK_RATE=${GAME_TICK_RATE} DEVOURSPHERE_SPHERE_TARGET_PX=${GAME_SPHERE_PX} DEVOURSPHERE_MAX_WIRE=${GAME_WIRE_MAX})

set(DS_ROOT /opt/devour-sphere)
# The game core is a patched copy (third_party/devoursphere_core, see PATCHES.md): a hook that runs
# right after a band is cleared lets src/world/planet.cpp paint the world's surface.
set(DS_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/devoursphere_core)

set(SHAPOGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SHAPOGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${DS_ROOT}/submodule/shapo-gfx shapogfx)
add_subdirectory(${DS_CORE_DIR} devoursphere_core)

foreach(lib shapogfx devoursphere_sim devoursphere_render)
    target_compile_options(${lib} PRIVATE
        -ffunction-sections -fdata-sections
        -fno-exceptions -fno-unwind-tables -fno-rtti)
endforeach()

target_compile_definitions(shapogfx PUBLIC
    SHAPOGFX_FORMAT_GRAY1=0
    SHAPOGFX_FORMAT_RGB444=0
    SHAPOGFX_FORMAT_ARGB4444=0
    SHAPOGFX_FORMAT_RGB565_SWAPPED=1)

if(GAME_GFX_FIXED)
    target_compile_definitions(shapogfx PRIVATE SHAPOGFX3D_FIXED_POINT=1)
endif()
if(GAME_GFX_HOT)
    target_compile_definitions(shapogfx PRIVATE
        "SHAPOGFX3D_HOT_ATTR=__attribute__((section(\".time_critical.gfx3d\")))"
        SHAPOGFX3D_HOT_INSTANTIATE=1)
endif()
if(NOT "${GAME_SIM_OPT}" STREQUAL "")
    target_compile_options(devoursphere_sim PRIVATE ${GAME_SIM_OPT})
endif()
if(NOT "${GAME_RENDER_OPT}" STREQUAL "")
    target_compile_options(devoursphere_render PRIVATE ${GAME_RENDER_OPT})
endif()
if(NOT "${GAME_GFX_OPT}" STREQUAL "")
    target_compile_options(shapogfx PRIVATE ${GAME_GFX_OPT})
endif()

target_compile_definitions(shapogfx PRIVATE
    SHAPOGFX3D_TEXTURE=$<BOOL:${GAME_TEXTURES}>
    SHAPOGFX2D_TRANSFORM=0
    SHAPOGFX2D_COLOR_KEY=0)
set(GAME_SOURCES
    src/main.cpp
    src/board/spi_bus.cpp
    src/board/clock.cpp
    src/board/fault.cpp
    src/board/sleep.cpp
    src/board/core_temp.cpp
    src/display/ili9341.cpp
    src/audio/sound.cpp
    src/input/buttons.cpp
    src/input/joystick.cpp
    src/storage/high_score.cpp
    src/game/game_loop.cpp
    src/game/fps_overlay.cpp
    src/game/spi_test.cpp
    src/world/planet_core.cpp
    src/world/planet.cpp
    src/ui/intro.cpp
    src/ui/status.cpp
)

add_executable(game ${GAME_SOURCES})

target_include_directories(game PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}        # board_pins.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src)   # "display/ili9341.h", "audio/sound.h", ...

target_compile_definitions(game PRIVATE
    GAME_SYS_CLOCK_KHZ=${GAME_SYS_CLOCK_KHZ}
    GAME_SIM_ON_CORE1=$<BOOL:${GAME_SIM_ON_CORE1}>
    LCD_SPI_HZ=${GAME_LCD_SPI_HZ}u
    LCD_BAND_H_CFG=${GAME_BAND_H}
    JOY_OVERSAMPLE=${GAME_JOY_OVERSAMPLE}
    JOY_POLL_US=${GAME_JOY_POLL_US}
    JOY_INVERT_UD=${GAME_JOY_INVERT_UD}
    GAME_VREG_OVERRIDE=${GAME_VREG_OVERRIDE}
    GAME_FLASH_DIV=${GAME_FLASH_DIV}
    GAME_BENCH_AUTORUN=$<BOOL:${GAME_BENCH_AUTORUN}>
    GAME_SPI_TEST=$<BOOL:${GAME_SPI_TEST}>
    GAME_SKIP_STATIC=$<BOOL:${GAME_SKIP_STATIC}>
    GAME_BENCH_ENABLE=$<BOOL:${GAME_BENCH_ENABLE}>
    GAME_INPUT_TEST=$<BOOL:${GAME_INPUT_TEST}>
    GAME_TEMP_TEST=$<BOOL:${GAME_TEMP_TEST}>
    GAME_LATE_LAUNCH=$<BOOL:${GAME_LATE_LAUNCH}>
    GAME_PLANET_TEX=$<BOOL:${GAME_PLANET_TEX}>
    PLANET_SHARE=$<AND:$<BOOL:${GAME_PLANET_SHARE}>,$<BOOL:${GAME_SIM_ON_CORE1}>>
    PLANET_SHARE_ROWS=${GAME_PLANET_SHARE_ROWS}
    BODY_LIGHT=$<BOOL:${GAME_BODY_LIGHT}>
    BODY_LIGHT_AMB=${GAME_BODY_LIGHT_AMB}
    BODY_LIGHT_SUN=${GAME_BODY_LIGHT_SUN}
    PLANET_ANIM=$<AND:$<BOOL:${GAME_PLANET_ANIM}>,$<BOOL:${GAME_SIM_ON_CORE1}>>
    GAME_INTRO=$<BOOL:${GAME_INTRO}>
    GAME_SLEEP=$<BOOL:${GAME_SLEEP}>
    SLEEP_IDLE_S=${GAME_SLEEP_IDLE_S}
    GAME_ARENA_GUARD=${GAME_ARENA_GUARD}
    GAME_RAM_CODE_CFG="${GAME_RAM_CODE}"
    GAME_RAM_RODATA_CFG="${GAME_RAM_RODATA}"
    GAME_COPY_TO_RAM_CFG=$<BOOL:${GAME_COPY_TO_RAM}>
    LCD_LANDSCAPE=${GAME_LCD_LANDSCAPE}
    LCD_INVERT=${GAME_LCD_INVERT}
    LCD_BSWAP=${GAME_LCD_BSWAP})

if(NOT "${GAME_LCD_MADCTL}" STREQUAL "")
    target_compile_definitions(game PRIVATE LCD_MADCTL=${GAME_LCD_MADCTL})
endif()

target_link_libraries(game
    devoursphere::render
    hardware_spi
    hardware_gpio
    hardware_adc
    hardware_pwm
    hardware_dma
    hardware_clocks
    hardware_watchdog
    hardware_flash
    pico_flash
    hardware_vreg
    hardware_xosc
    hardware_powman
    hardware_rosc
    hardware_pll
    hardware_structs
    pico_rand
    pico_multicore
    pico_stdlib
)

target_compile_options(game PRIVATE
    -Wall -Wextra
    -ffunction-sections -fdata-sections
    -fno-exceptions -fno-unwind-tables
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)

pico_enable_stdio_usb(game 1)
pico_enable_stdio_uart(game 0)

if (PICO_CYW43_SUPPORTED)
    target_link_libraries(game pico_cyw43_arch_none)
endif()

# Where the code runs. XIP (flash through a 16 KB cache shared by both cores) is
# the SDK default; the libraries named in GAME_RAM_CODE / GAME_RAM_RODATA are
# moved to SRAM by overriding the SDK linker-script exclude lists, and
# GAME_COPY_TO_RAM moves everything.
function(game_ram_patterns OUT)
    set(pats "")
    string(REPLACE "+" ";" ARGN_LIST "${ARGN}")
    foreach(t ${ARGN_LIST})
        if(t STREQUAL "sim")
            string(APPEND pats " *libdevoursphere_sim.a:")
        elseif(t STREQUAL "render")
            string(APPEND pats " *libdevoursphere_render.a:")
        elseif(t STREQUAL "gfx")
            string(APPEND pats " *libshapogfx.a:")
        elseif(t STREQUAL "gfx3d")
            string(APPEND pats " *libshapogfx.a:*gfx3d*")
        elseif(NOT t STREQUAL "")
            message(FATAL_ERROR "unknown RAM group '${t}' (sim, render, gfx, gfx3d)")
        endif()
    endforeach()
    set(${OUT} "${pats}" PARENT_SCOPE)
endfunction()

if(NOT "${GAME_PLANET_DEFS}" STREQUAL "")
    string(REPLACE "+" ";" PLANET_DEF_LIST "${GAME_PLANET_DEFS}")
    set_source_files_properties(src/world/planet_core.cpp PROPERTIES COMPILE_DEFINITIONS "${PLANET_DEF_LIST}")
endif()

if(GAME_COPY_TO_RAM)
    pico_set_binary_type(game copy_to_ram)
else()
    game_ram_patterns(RAM_TEXT_PATS ${GAME_RAM_CODE})
    game_ram_patterns(RAM_RO_PATS ${GAME_RAM_RODATA})
    set(LD_DIR ${CMAKE_BINARY_DIR}/ld)
    file(MAKE_DIRECTORY ${LD_DIR})
    file(WRITE ${LD_DIR}/default_text_excludes.incl
        "*(EXCLUDE_FILE(*libgcc.a: *libm.a:${RAM_TEXT_PATS}) .text*)\n")
    file(WRITE ${LD_DIR}/default_rodata_excludes.incl
        "*(EXCLUDE_FILE(*libgcc.a: *libm.a:${RAM_RO_PATS}) .rodata*)\n")
    pico_add_linker_script_override_path(game ${LD_DIR}
        FILES default_text_excludes.incl default_rodata_excludes.incl)
endif()

pico_add_extra_outputs(game)

# The firmware image must end before the music area (flash offset 0x80000, see src/audio/sound.cpp)
add_custom_command(TARGET game POST_BUILD
    COMMAND sh -c "[ $(stat -c %s ${CMAKE_BINARY_DIR}/game.bin) -le 524288 ] || { echo 'game.bin is larger than 512 KB: it would overlap the music area'; exit 1; }"
    VERBATIM)
