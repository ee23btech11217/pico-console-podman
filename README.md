# pico-console-podman

Rootless podman dev container for the Pico 2 (RP2350) game: pico-sdk, picotool, FreeRTOS, ILI9341 driver.

## Layout
```
pico-console-podman/
├── Containerfile   # recipe for the base image
├── run.sh          # container manager (this README)
├── project/        # your code, mounted at /workspace (lives on the host)
└── backups/        # created by ./run.sh backup
```

## Quick start
```bash
chmod +x run.sh
./run.sh            # builds the image on first run, then opens the container
```
Inside the container, work in `/workspace`.

## Commands
| Command | What it does |
|---|---|
| `./run.sh` | Start the container. Resumes it if it exists, or opens a second shell if it is already running |
| `./run.sh save` | Snapshot the container's files into the image `pico-console-dev:saved` |
| `./run.sh diff` | List files changed in the container (A=added, C=changed, D=deleted) |
| `./run.sh backup` | Export the saved image to `backups/pico-console-dev-<date>.tar.gz` |
| `./run.sh push` | Push the saved image (Claude login removed, squashed) to `ghcr.io/ee23btech11217/pico-console-dev:latest`; override with `REGISTRY_IMAGE=...` |
| `./run.sh restore <file>` | Load an image from a backup file |
| `./run.sh reset` | Delete the container (a fresh one is created next start) |
| `./run.sh rebuild` | Rebuild the base image from `Containerfile` |

## How persistence works
- The container is **not** removed on exit, so anything you install stays.
- `save` makes that permanent: new containers start from `pico-console-dev:saved` if it exists, otherwise from the base image.
- `/workspace` is a host folder (`project/`), so it is never part of the image. Keep it in git.
- Variables set with `export` are lost. Put permanent ones in `PERSIST_ENV` at the top of `run.sh`, or in `~/.bashrc` inside the container.

## Typical workflow
```bash
./run.sh            # install/change things inside the container
./run.sh save       # from a second host terminal, or after exiting
./run.sh backup     # optional safety copy
```
Start over from the Containerfile: `./run.sh reset`, then `podman rmi pico-console-dev:saved`, then `./run.sh rebuild`.

## Flashing (picotool)
USB is passed through with `--device /dev/bus/usb`. Put the Pico in BOOTSEL mode, then:
```bash
picotool load -fx build/game.uf2
```
If the device is not visible, replug it and check host udev rules.

## Notes
- Saved images can contain files from the container's home folder, including a Claude Code login. Log out before sharing an image or backup.
- Anything you want long-term (new tools, SDK clones) should also go in the `Containerfile`.
