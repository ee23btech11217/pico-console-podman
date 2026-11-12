# pico-console-podman

A ready-to-use, rootless [Podman](https://podman.io) development environment for the **Raspberry Pi Pico 2 (RP2350)**, plus the project it was built for: **Devour Sphere**, a game running on a Pico 2 with an ILI9341 TFT display, an analog joystick and push buttons.

The container bundles everything needed to build and flash firmware, so you don't have to set up an ARM toolchain on your machine.

## What's inside the container

- Ubuntu 24.04 with `gcc-arm-none-eabi`, CMake, Ninja
- [pico-sdk](https://github.com/raspberrypi/pico-sdk) and [picotool](https://github.com/raspberrypi/picotool) (built from source, RP2350 supported)
- [PicoLibSDK](https://github.com/Panda381/PicoLibSDK) and the [ILI9341](https://github.com/Matiasus/ILI9341) driver (reference)
- Node.js and the [Claude Code](https://claude.com/claude-code) CLI (optional)
- A non-root `dev` user (UID 1000) and USB passthrough for flashing

## Repository layout

```
.
├── Containerfile    # recipe for the dev image
├── run.sh           # container manager (start, save, push, backup, ...)
├── 99-pico.rules    # udev rules for the host, so the Pico is accessible
└── project/         # the Devour Sphere game; mounted at /workspace
    ├── src/  tools/  docs/  CMakeLists.txt  Makefile
    └── README.md    # hardware wiring, design notes, performance results
```

## Getting started

Requirements: Linux, [Podman](https://podman.io/docs/installation) (rootless), and `git`.

### 1. Get the repo and the image

```bash
git clone https://github.com/ee23btech11217/pico-console-podman.git
cd pico-console-podman
```

Either pull the prebuilt image (public):

```bash
podman pull ghcr.io/ee23btech11217/pico-console-dev:latest
podman tag  ghcr.io/ee23btech11217/pico-console-dev:latest pico-console-dev:saved
```

or build it yourself (takes a few minutes, it clones the SDKs):

```bash
./run.sh rebuild
```

### 2. Install the udev rule on the host (once)

This lets the container see the Pico in BOOTSEL mode and its USB serial port:

```bash
sudo cp 99-pico.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

### 3. Start the container

```bash
./run.sh
```

Your `project/` folder is mounted at `/workspace`. Inside the container:

```bash
cd /workspace
make            # see project/Makefile for targets
```

### 4. Flash

Hold BOOTSEL while plugging in the Pico 2, then:

```bash
picotool load -fx out/game.uf2
```

## `run.sh` commands

| Command | What it does |
|---|---|
| `./run.sh` | Start the container, resume it, or open another shell in it |
| `./run.sh save` | Snapshot the container into the image `pico-console-dev:saved` |
| `./run.sh diff` | List files changed in the container |
| `./run.sh backup` | Export the saved image to `backups/*.tar.gz` |
| `./run.sh restore <file>` | Load an image from a backup file |
| `./run.sh push` | Push the saved image (login removed, squashed) to `$REGISTRY_IMAGE` |
| `./run.sh update` | Upgrade Claude Code inside the container |
| `./run.sh reset` | Delete the container; the next start creates a fresh one |
| `./run.sh rebuild` | Rebuild the base image from the `Containerfile` |

The container is not removed on exit, so what you install persists. `./run.sh save` makes it permanent for new containers. Set `REGISTRY_IMAGE` to push to your own registry.

## Using Claude Code in the container (optional)

A browser login can't complete inside a container. Instead, run `claude setup-token` on your host and save the token to a file named `.claude-token` next to `run.sh`. It is mounted read-only into the container and is git-ignored, so it is never baked into an image or committed. **Never commit this file.**

## The project

See [`project/README.md`](project/README.md) for the hardware wiring (ILI9341 over SPI, joystick, buttons), architecture and measured performance (about 49 FPS at 400 MHz). Notes, charts and a presentation are in `project/docs/`.

## Notes

- Images saved from a running container may contain files from its home folder. `./run.sh push` strips the Claude login, but log out before sharing any image or backup yourself.
- Not affiliated with Raspberry Pi Ltd. The RP2350 datasheet is not included; download it from [raspberrypi.com](https://www.raspberrypi.com/documentation/microcontrollers/).
- Music credits are in `project/docs/music/CREDITS.md`.
