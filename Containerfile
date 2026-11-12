# Containerfile — Raspberry Pi Pico game-console dev environment
# Build:  podman build -t pico-console-dev -f Containerfile .

FROM ubuntu:24.04

ARG PICO_SDK_REPO=https://github.com/raspberrypi/pico-sdk
ARG PICO_SDK_BRANCH=master
ARG PICOLIBSDK_REPO=https://github.com/Panda381/PicoLibSDK
ARG PICOLIBSDK_BRANCH=main
ARG ILI9341_REPO=https://github.com/Matiasus/ILI9341
ARG ILI9341_BRANCH=master
ARG PICOTOOL_REPO=https://github.com/raspberrypi/picotool
ARG PICOTOOL_BRANCH=master
ARG NODE_MAJOR=20

ENV DEBIAN_FRONTEND=noninteractive \
    PICO_SDK_PATH=/opt/pico-sdk \
    PICOLIBSDK_PATH=/opt/PicoLibSDK \
    ILI9341_PATH=/opt/ILI9341 \
    PICO_BOARD=pico2 \
    PICO_PLATFORM=rp2350

# ---- base toolchain: compiler, cmake, picotool build deps ----
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git python3 python3-pip pkg-config \
        gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib \
        libusb-1.0-0-dev udev usbutils minicom screen \
        ca-certificates curl gnupg sudo vim nano less unzip \
    && rm -rf /var/lib/apt/lists/*

# ---- Node.js (for the Claude Code CLI) ----
RUN curl -fsSL https://deb.nodesource.com/setup_${NODE_MAJOR}.x | bash - \
    && apt-get install -y --no-install-recommends nodejs \
    && rm -rf /var/lib/apt/lists/*

# ---- non-root dev user (podman rootless-friendly) ----
# Ubuntu 24.04's base image ships a built-in "ubuntu" user already at UID/GID
# 1000 — remove it first so "dev" can take 1000/1000 instead. This must match
# the uid=/gid= values passed to --userns=keep-id in run.sh, or USB device
# permissions (ACLs, group ownership) won't line up with the container's user.
RUN userdel -r ubuntu 2>/dev/null || true; groupdel ubuntu 2>/dev/null || true
RUN groupadd -g 1000 dev \
    && useradd -m -u 1000 -g 1000 -s /bin/bash -G sudo,dialout,plugdev dev \
    && echo "dev ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/dev

# ---- pico-sdk (picotool builds against it, so clone this first) ----
RUN git clone --branch ${PICO_SDK_BRANCH} --depth 1 ${PICO_SDK_REPO} ${PICO_SDK_PATH} \
    && cd ${PICO_SDK_PATH} && git submodule update --init --depth 1

# ---- picotool (built from source so pico2 / RP2350 is fully supported) ----
RUN git clone --depth 1 --branch ${PICOTOOL_BRANCH} ${PICOTOOL_REPO} /opt/picotool \
    && cd /opt/picotool && mkdir build && cd build \
    && cmake -DPICO_SDK_PATH=${PICO_SDK_PATH} .. \
    && make -j"$(nproc)" \
    && make install \
    && ldconfig

# NOTE: picotool's udev rule is NOT installed here on purpose — udev runs on
# the host, not inside the container, so a copy here has no effect on device
# permissions. Install it on the podman HOST instead; see README step 3.

# ---- PicoLibSDK (alternative lightweight SDK) ----
RUN git clone --branch ${PICOLIBSDK_BRANCH} --depth 1 ${PICOLIBSDK_REPO} ${PICOLIBSDK_PATH} \
    && cd ${PICOLIBSDK_PATH} && (git submodule update --init --depth 1 || true)

# ---- ILI9341 driver reference repo ----
RUN git clone --branch ${ILI9341_BRANCH} --depth 1 ${ILI9341_REPO} ${ILI9341_PATH} \
    || git clone --depth 1 ${ILI9341_REPO} ${ILI9341_PATH}

RUN chown -R dev:dev /opt/pico-sdk /opt/PicoLibSDK /opt/ILI9341 /opt/picotool

USER dev
WORKDIR /home/dev
ENV PATH="/home/dev/.npm-global/bin:${PATH}"
RUN npm config set prefix '/home/dev/.npm-global' \
    && npm install -g @anthropic-ai/claude-code

WORKDIR /workspace
CMD ["/bin/bash"]
