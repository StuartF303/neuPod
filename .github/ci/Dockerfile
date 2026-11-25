# neuPod CI toolchain image: ghcr.io/stuartf303/neupod-ci (built by .github/workflows/ci-image.yml).
# The rp2350, host and y2 jobs of firmware.yml run inside it, so they never run apt and use one pinned compiler set
# (the rp2350 size budgets and the core-1 closure check depend on the compiler). Versions are listed in
# /etc/neupod-ci-versions, which every job prints.
#
# ubuntu:24.04 pinned by digest (looked up 2026-11-11). Refresh with:
#   docker buildx imagetools inspect ubuntu:24.04   (the top-level "Digest:")
FROM ubuntu:24.04@sha256:fe81560990fae86dc4743202b2cb4d3df98d0bcf949173cecef15ce1127ef852

# The one place the Pico SDK version is set (firmware.yml no longer fetches the SDK itself).
ARG PICO_SDK_VERSION=2.3.1

ENV DEBIAN_FRONTEND=noninteractive \
    PICO_SDK_PATH=/opt/pico-sdk

# ci-apt.sh probes for a reachable Ubuntu mirror and retries (the probe needs curl, hence the two passes).
COPY scripts/ci-apt.sh /usr/local/bin/ci-apt.sh
RUN ci-apt.sh ca-certificates curl \
 && ci-apt.sh \
        build-essential cmake ninja-build python3 git file procps \
        libsdl2-dev \
        gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib \
        gcc-arm-linux-gnueabihf libc6-dev-armhf-cross \
 && rm -rf /var/lib/apt/lists/*

# Full Pico SDK with all its submodules (rp2350 needs btstack, cyw43, lwip, mbedtls, tinyusb; host and y2 need mbedtls).
RUN git clone --depth 1 --branch "$PICO_SDK_VERSION" https://github.com/raspberrypi/pico-sdk.git "$PICO_SDK_PATH" \
 && git -C "$PICO_SDK_PATH" submodule update --init --depth 1

# actions/checkout in a container runs git as a different owner than the workspace.
RUN git config --system --add safe.directory '*'

RUN { \
      echo "image:     ubuntu $(. /etc/os-release; echo "$VERSION")"; \
      echo "pico-sdk:  $PICO_SDK_VERSION ($(git -C "$PICO_SDK_PATH" rev-parse --short=12 HEAD))"; \
      echo "cmake:     $(cmake --version | head -1)"; \
      echo "ninja:     $(ninja --version)"; \
      echo "gcc:       $(gcc --version | head -1)"; \
      echo "python3:   $(python3 --version)"; \
      echo "arm-none-eabi-gcc:       $(arm-none-eabi-gcc --version | head -1)"; \
      echo "arm-linux-gnueabihf-gcc: $(arm-linux-gnueabihf-gcc --version | head -1)"; \
      echo "newlib:    $(dpkg-query -W -f='${Version}' libnewlib-arm-none-eabi)"; \
      echo "sdl2-dev:  $(dpkg-query -W -f='${Version}' libsdl2-dev)"; \
      echo "git:       $(git --version)"; \
    } > /etc/neupod-ci-versions

WORKDIR /work
