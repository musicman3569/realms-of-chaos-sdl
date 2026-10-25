# The AppImage's build environment: Ubuntu 22.04 (glibc 2.35), so the program runs on any Linux
# from about 2022 on (SteamOS 3, Bazzite, Fedora, Ubuntu, Arch...), with SDL2 built from source.
# SDL loads X11, Wayland, PulseAudio, PipeWire and ALSA from the system at run time, so their
# headers are here but none of them goes into the AppImage.
FROM ubuntu:22.04

ARG SDL_VERSION=2.32.10
ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build pkg-config ca-certificates curl file \
        desktop-file-utils \
        libasound2-dev libpulse-dev libpipewire-0.3-dev \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev \
        libxss-dev libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev \
        libdrm-dev libgbm-dev libgl1-mesa-dev libegl1-mesa-dev libgles2-mesa-dev \
        libdbus-1-dev libudev-dev \
    && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL -o /tmp/sdl.tar.gz \
        https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VERSION}/SDL2-${SDL_VERSION}.tar.gz \
    && tar -xzf /tmp/sdl.tar.gz -C /tmp \
    && cmake -S /tmp/SDL2-${SDL_VERSION} -B /tmp/sdl-build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/sdl2 \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF \
    && cmake --build /tmp/sdl-build \
    && cmake --install /tmp/sdl-build \
    && rm -rf /tmp/sdl.tar.gz /tmp/SDL2-${SDL_VERSION} /tmp/sdl-build

# appimagetool (it runs extracted: there's no FUSE in a container), and the runtime it puts at
# the front of the AppImage: the static one, which doesn't need libfuse2 on the player's system
RUN curl -fsSL -o /opt/appimagetool.AppImage \
        https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage \
    && curl -fsSL -o /opt/runtime-x86_64 \
        https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64 \
    && chmod +x /opt/appimagetool.AppImage
ENV APPIMAGE_EXTRACT_AND_RUN=1
