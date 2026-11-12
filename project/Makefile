PICO_SDK_PATH ?= /opt/pico-sdk
BUILD_DIR ?= out
TARGET ?= game
PICOTOOL ?= picotool
JOBS ?= $(shell nproc)
SIZE ?= arm-none-eabi-size
OBJDUMP ?= arm-none-eabi-objdump
# Serial log (USB CDC printf). /hostdev is the host /dev mounted by run.sh.
LOG_SECS ?= 20
LOG_DEV ?= /hostdev/ttyACM*
# 1 = build ShapoGFX with texture/env-map support (GAME_TEXTURES), 0 = off.
TEXTURES ?= 0
# Sphere mesh cell size in px (smaller = smoother sphere, more lines) and wireframe line array size.
SPHERE_PX ?= 24
WIRE_MAX ?= 1100

# The shipping firmware (the one the presentation describes, README "Make targets"). Passed on every build, so no
# setting comes from a stale CMake cache. CMAKE_ARGS is added after it and wins (tools/perf_run.py relies on that).
SHIP_ARGS ?= -DGAME_SYS_CLOCK_KHZ=264000 -DGAME_LCD_SPI_HZ=66000000 -DGAME_SIM_ON_CORE1=ON -DGAME_LATE_LAUNCH=ON \
             -DGAME_PLANET_TEX=ON -DGAME_PLANET_SHARE=ON -DGAME_PLANET_SHARE_ROWS=4 -DGAME_PLANET_ANIM=ON \
             -DGAME_BENCH_AUTORUN=OFF -DGAME_BENCH_ENABLE=OFF -DGAME_INPUT_TEST=OFF -DGAME_TEMP_TEST=OFF \
             -DGAME_SPI_TEST=OFF -DGAME_PLANET_DEFS= -DGAME_ARENA_GUARD=0 \
             -DGAME_LCD_MADCTL=0xA0 -DGAME_LCD_INVERT=0 -DGAME_LCD_BSWAP=1 -DGAME_JOY_INVERT_UD=1 \
             -DGAME_BODY_LIGHT=ON -DGAME_BODY_LIGHT_AMB=45 -DGAME_BODY_LIGHT_SUN=80
# Extra cmake flags on top, e.g. make flash CMAKE_ARGS="-DGAME_SYS_CLOCK_KHZ=200000"
CMAKE_ARGS ?=

# Wait for the Pico in BOOTSEL. Each try is a fresh picotool process: a long-lived one never notices the Pico
# re-enumerating inside a rootless container (libusb hotplug events don't cross the network namespace). On this board
# `picotool info` can exit with an error in BOOTSEL ("Block loop is not valid"), so only "not found" counts as absent.
# No sudo: access comes from the host udev rule (podman/99-pico.rules).
define WAIT_BOOTSEL
@ok=0; for i in $$(seq 1 30); do \
	  out=$$($(PICOTOOL) info 2>&1); \
	  case "$$out" in *"No accessible"*|*"Unable to"*) ;; *) ok=1; break;; esac; sleep 0.5; \
	done; \
	[ $$ok = 1 ] || { echo "Pico not found in BOOTSEL after 15 s. Try: lsusb; picotool info"; exit 1; }
endef

.PHONY: all help configure build clean flash log run music music-clear bench spitest size info erase

all: build

help:
	@echo "make              build the shipping firmware (JOBS=$(JOBS))"
	@echo "make flash        build, reboot the Pico into BOOTSEL by itself, load and run"
	@echo "make log          print USB printf output for LOG_SECS=$(LOG_SECS) seconds"
	@echo "make run          flash, then log"
	@echo "make music        pack the tracks of $(MUSIC_DIR) (whole tracks) and load them into flash"
	@echo "make music-clear  remove the music pack (synthesized tune again)"
	@echo "make bench        flash the self-running benchmark build and read the log"
	@echo "make spitest      flash the on-screen SPI-rate test pattern"
	@echo "make size         section sizes and section headers of $(TARGET).elf"
	@echo "make info         picotool info -a"
	@echo "make erase        erase the whole flash (also the music and the high scores)"
	@echo "make clean        remove $(BUILD_DIR)"
	@echo ""
	@echo "variables (make <target> VAR=value):"
	@echo "  CMAKE_ARGS=...   extra cmake flags on top of the shipping defaults, e.g. -DGAME_SYS_CLOCK_KHZ=200000"
	@echo "  LOG_SECS=n       seconds of USB log to read, default $(LOG_SECS)"
	@echo "  JOBS=n           parallel build jobs, default $(JOBS)"
	@echo "  TEXTURES=0|1     ShapoGFX texture support, default $(TEXTURES)"
	@echo "  SPHERE_PX=n      sphere mesh cell size in px, default $(SPHERE_PX)"
	@echo "  WIRE_MAX=n       sphere line array size, default $(WIRE_MAX)"

configure:
	cmake -S . -B $(BUILD_DIR) -DPICO_PLATFORM=rp2350 -DPICO_BOARD=pico2 -DPICO_SDK_PATH=$(PICO_SDK_PATH) \
	  -DGAME_TEXTURES=$(TEXTURES) -DGAME_SPHERE_PX=$(SPHERE_PX) -DGAME_WIRE_MAX=$(WIRE_MAX) $(SHIP_ARGS) $(CMAKE_ARGS)

build: configure
	cmake --build $(BUILD_DIR) -j$(JOBS)

clean:
	rm -rf $(BUILD_DIR)

flash: build
	-$(PICOTOOL) reboot -f -u
	$(WAIT_BOOTSEL)
	$(PICOTOOL) load -x $(BUILD_DIR)/$(TARGET).uf2

# Wait up to 10 s for the CDC port to enumerate after a reboot, then dump output.
log:
	@dev=""; \
	for i in $$(seq 1 20); do \
	  dev=$$(ls $(LOG_DEV) 2>/dev/null | head -n1); \
	  [ -n "$$dev" ] && break; sleep 0.5; \
	done; \
	[ -n "$$dev" ] || { echo "no $(LOG_DEV): is the Pico running and was the container started by the new run.sh?"; exit 1; }; \
	echo ">> reading $$dev for $(LOG_SECS)s"; \
	stty -F $$dev raw -echo; \
	timeout $(LOG_SECS) cat $$dev || true

run: flash log

# make music    : pack the MP3/WAV files of MUSIC_DIR (MUSIC_WINDOW = seconds of each, 0 = whole tracks) and load them
#                 into the music area of the flash (0x10080000). `make flash` never touches it, so this is needed only
#                 when the tracks change. Wait ~10 s after the last boot before running it.
MUSIC_DIR ?= docs/music
MUSIC_WINDOW ?= 0
MUSIC_FILES ?= $(sort $(wildcard $(MUSIC_DIR)/*.mp3))
music:
	python3 tools/music_pack.py --window $(MUSIC_WINDOW) -o $(BUILD_DIR)/music.bin $(MUSIC_FILES)
	-$(PICOTOOL) reboot -f -u
	$(WAIT_BOOTSEL)
	$(PICOTOOL) load -o 0x10080000 $(BUILD_DIR)/music.bin
	$(PICOTOOL) reboot

# make music-clear : remove the music pack (the synthesized tune plays again)
music-clear:
	-$(PICOTOOL) reboot -f -u
	$(WAIT_BOOTSEL)
	$(PICOTOOL) erase -r 0x10080000 0x10081000
	$(PICOTOOL) reboot

# make bench    : the self-running benchmark build, then the log (results re-print every few seconds)
bench:
	$(MAKE) flash CMAKE_ARGS="$(CMAKE_ARGS) -DGAME_BENCH_AUTORUN=ON"
	$(MAKE) log LOG_SECS=$${LOG_SECS:-100}

# make spitest  : the on-screen SPI-rate test pattern (look at the LCD; the log prints the rates)
spitest:
	$(MAKE) flash CMAKE_ARGS="$(CMAKE_ARGS) -DGAME_SPI_TEST=ON"
	$(MAKE) log LOG_SECS=20

size: build
	$(SIZE) -A $(BUILD_DIR)/$(TARGET).elf
	$(OBJDUMP) -h $(BUILD_DIR)/$(TARGET).elf

info:
	$(PICOTOOL) info -a

erase:
	$(PICOTOOL) erase
