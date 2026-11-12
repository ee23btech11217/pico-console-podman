#!/usr/bin/env bash
# pico-console-dev container manager (rootless podman).
#
# Usage:
#   ./run.sh            start (or resume / re-enter) the container
#   ./run.sh save       snapshot the current container into image pico-console-dev:saved
#   ./run.sh reset      delete the container (next start uses the :saved image if present)
#   ./run.sh rebuild    rebuild the base image from Containerfile
#   ./run.sh diff       list every file the container changed vs its image
#   ./run.sh backup     export the :saved image to backups/*.tar.gz
#   ./run.sh update     upgrade Claude Code inside the container (new models need new versions)
#   ./run.sh restore F  load an image from a backup file F
#   ./run.sh push       push the :saved image (Claude login removed) to $REGISTRY_IMAGE
set -euo pipefail

IMAGE=pico-console-dev
SAVED="$IMAGE:saved"
NAME=pico-console-dev
# Where `./run.sh push` publishes the image (pull it with: podman pull <this>).
REGISTRY_IMAGE="${REGISTRY_IMAGE:-ghcr.io/ee23btech11217/pico-console-dev:latest}"
BACKUP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/backups"
# Extra environment variables to bake into the saved image (export-only vars are not captured).
PERSIST_ENV=(
    "FREERTOS_KERNEL_PATH=/opt/FreeRTOS-Kernel"
)
# Free-Claude-Code (fcc) installs write a proxy-only model name into
# ~/.claude/settings.json. Plain `claude` can't use it (no proxy), which shows up
# as "issue with the selected model" and looks like an auth failure. Strip it on
# every container start; `fcc-claude` sets its own model, so it is unaffected.
FIX_CLAUDE='f=$HOME/.claude/settings.json; [ -f "$f" ] && python3 - "$f" <<PY
import json,sys
p=sys.argv[1]; d=json.load(open(p))
if "freecc" in str(d.get("model","")):
    d.pop("model",None); d.pop("modelSettings",None)
    json.dump(d,open(p,"w"),indent=2); print(">> Removed free-claude-code model pin from settings.json")
PY
python3 - <<PY
import json,os
p=os.path.expanduser("~/.claude.json")
d=json.load(open(p)) if os.path.exists(p) else {}
d["hasCompletedOnboarding"]=True
d.setdefault("projects",{}).setdefault("/workspace",{})["hasTrustDialogAccepted"]=True
json.dump(d,open(p,"w"))
PY
l="[ -r /run/claude-token ] && export CLAUDE_CODE_OAUTH_TOKEN=\"\$(cat /run/claude-token)\""
grep -qF "$l" ~/.bashrc || echo "$l" >> ~/.bashrc
eval "$l"
exec bash'
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/project"
# Browser login inside the container can't complete (OAuth redirect goes to the
# container's localhost). Instead run `claude setup-token` on the HOST and save
# the token to .claude-token next to this script. It is bind-mounted read-only
# (not an env var), so `save`/`backup` never bake it into the image.
TOKEN_FILE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/.claude-token"
cmd="${1:-start}"

build_base() {
    echo ">> Building $IMAGE (this clones the SDKs, a few minutes)"
    podman build -t "$IMAGE" -f Containerfile .
}

case "$cmd" in
save)
    podman container exists "$NAME" || { echo "No container '$NAME' to save. Run ./run.sh first."; exit 1; }
    echo ">> Saving container '$NAME' -> $SAVED (all files changed in the container)"
    CHANGE_ARGS=()
    for kv in "${PERSIST_ENV[@]}"; do CHANGE_ARGS+=(--change "ENV $kv"); done
    podman commit ${CHANGE_ARGS[@]+"${CHANGE_ARGS[@]}"} "$NAME" "$SAVED"
    echo ">> Saved. New containers will start from $SAVED."
    echo "   (/workspace is a mounted folder and is not part of the image.)"
    exit 0
    ;;
reset)
    podman rm -f "$NAME" >/dev/null 2>&1 || true
    echo ">> Container removed. Next ./run.sh creates a fresh one."
    exit 0
    ;;
rebuild)
    build_base
    exit 0
    ;;
diff)
    podman container exists "$NAME" || { echo "No container '$NAME'."; exit 1; }
    podman diff "$NAME"   # A=added C=changed D=deleted
    exit 0
    ;;
backup)
    podman image exists "$SAVED" || { echo "No $SAVED image. Run ./run.sh save first."; exit 1; }
    mkdir -p "$BACKUP_DIR"
    OUT="$BACKUP_DIR/pico-console-dev-$(date +%F-%H%M).tar.gz"
    echo ">> Exporting $SAVED -> $OUT"
    podman save "$SAVED" | gzip > "$OUT"
    echo ">> Done ($(du -h "$OUT" | cut -f1))"
    exit 0
    ;;
restore)
    [ -f "${2:-}" ] || { echo "Usage: ./run.sh restore <backup.tar.gz>"; exit 1; }
    podman load -i "$2"
    exit 0
    ;;
update)
    podman container exists "$NAME" || { echo "No container '$NAME'. Run ./run.sh first."; exit 1; }
    was_running=$(podman inspect -f '{{.State.Running}}' "$NAME")
    [ "$was_running" = "true" ] || podman start "$NAME" >/dev/null
    podman exec "$NAME" npm install -g @anthropic-ai/claude-code@latest
    podman exec "$NAME" claude --version
    [ "$was_running" = "true" ] || podman stop "$NAME" >/dev/null
    echo ">> Run ./run.sh save to keep the upgrade in the image."
    exit 0
    ;;
push)
    podman image exists "$SAVED" || { echo "No $SAVED image. Run ./run.sh save first."; exit 1; }
    # Build a copy of :saved without the Claude Code login, squashed into one
    # layer so the deleted file is not still readable from a lower layer.
    tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
    cat > "$tmp/Containerfile" <<EOF
FROM localhost/$SAVED
RUN rm -f "\$HOME/.claude/.credentials.json" /root/.claude/.credentials.json 2>/dev/null || true
EOF
    echo ">> Building $REGISTRY_IMAGE from $SAVED (login removed, squashed)"
    podman build --squash-all -t "$REGISTRY_IMAGE" -f "$tmp/Containerfile" "$tmp"
    echo ">> Pushing $REGISTRY_IMAGE (log in first with: podman login ${REGISTRY_IMAGE%%/*})"
    podman push "$REGISTRY_IMAGE"
    echo ">> Done. Pull with: podman pull $REGISTRY_IMAGE"
    exit 0
    ;;
start) ;;
*)
    echo "Unknown command: $cmd"; sed -n '2,14p' "$0"; exit 1
    ;;
esac

# --- Already have a container? Resume or re-enter it ---
if podman container exists "$NAME"; then
    if [ "$(podman inspect -f '{{.State.Running}}' "$NAME")" = "true" ]; then
        echo ">> Container already running, opening another shell in it"
        exec podman exec -it "$NAME" bash
    fi
    echo ">> Resuming existing container (your installed changes are kept)"
    exec podman start -ai "$NAME"
fi

# --- Pick image: saved snapshot if present, else base (build if needed) ---
if podman image exists "$SAVED"; then
    RUN_IMAGE="$SAVED"
else
    podman image exists "$IMAGE" || build_base
    RUN_IMAGE="$IMAGE"
fi

# --- USB device passthrough for picotool / flashing ---
# NOTE: --device only copies the device nodes that exist when the container is
# created. The Pico re-enumerates (new bus address, new product id) every time
# it reboots into BOOTSEL, so those nodes would be missing. Bind-mounting the
# host directory instead keeps it live. ":dev" is needed because rootless
# podman otherwise mounts bind volumes "nodev".
#   /dev/bus/usb      -> picotool / libusb (BOOTSEL + reset interface)
#   /hostdev/ttyACM*  -> USB CDC printf output (read with minicom/screen/make log)
# Host side needs the udev rule in podman/99-pico.rules (see README).
USB_ARGS=()
if [ -d /dev/bus/usb ]; then
    USB_ARGS=(
        -v /dev/bus/usb:/dev/bus/usb:dev
        -v /dev:/hostdev:ro,dev
        --group-add keep-groups
    )
fi

TOKEN_ARGS=()
[ -s "$TOKEN_FILE" ] && TOKEN_ARGS=(-v "$TOKEN_FILE":/run/claude-token:ro,Z)

echo ">> Creating container from $RUN_IMAGE. Project files live in: $PROJECT_DIR"

# No --rm: the container persists after exit so changes survive.
podman run -it \
    --name "$NAME" \
    --userns=keep-id:uid=1000,gid=1000 \
    -v "$PROJECT_DIR":/workspace:Z \
    ${USB_ARGS[@]+"${USB_ARGS[@]}"} \
    ${TOKEN_ARGS[@]+"${TOKEN_ARGS[@]}"} \
    -w /workspace \
    "$RUN_IMAGE" \
    bash -c "$FIX_CLAUDE"
