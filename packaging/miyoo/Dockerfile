# The Miyoo Mini (Plus) build environment: the cross toolchain shauninman's
# union-miyoomini-toolchain installs (GCC 8.3 for arm-linux-gnueabihf, glibc 2.28, the vendor's
# MI libraries), in Ubuntu 22.04 for a CMake new enough for CMakeLists.txt; and SDL2 for the
# handheld: XK9274's sdl2_miyoo (SDL 2.0.20 with the MI_GFX / MI_AO drivers, as the Ports
# Collection's newer SDL2 ports bundle it), its prebuilt library and the headers of the same commit.
FROM ubuntu:22.04

ARG DEBIAN_FRONTEND=noninteractive
ARG TOOLCHAIN_URL=https://github.com/shauninman/miyoomini-toolchain-buildroot/releases/download/v0.0.3/miyoomini-toolchain.tar.xz
ARG TOOLCHAIN_SHA256=e158b6ebde0c933ecd57b36760fb8602fad4176a09fb61559dc7bcffe6624f2e
ARG SDL_COMMIT=178fc0fcfbdc8a92fbac16171fe945eb66d8f7d5
ARG SDL_LIB_SHA256=e2c78624c88b117e0613a49de00634b001287942357fd58606d2a0f249778b8e

RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake ninja-build ca-certificates curl xz-utils file zip imagemagick \
    && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL -o /tmp/toolchain.tar.xz "$TOOLCHAIN_URL" \
    && echo "$TOOLCHAIN_SHA256  /tmp/toolchain.tar.xz" | sha256sum -c - \
    && tar -xJf /tmp/toolchain.tar.xz -C /opt \
    && rm /tmp/toolchain.tar.xz

RUN short=$(echo "$SDL_COMMIT" | cut -c1-8) \
    && mkdir -p /opt/sdl2-miyoo/lib /opt/sdl2-miyoo/include/SDL2 /opt/sdl2-miyoo/share \
    && curl -fsSL -o /tmp/sdl-lib.tar.gz \
        "https://github.com/XK9274/sdl2_miyoo/releases/download/sdl2-miyoo-$short/libSDL2-miyoo-nogl-$short.tar.gz" \
    && echo "$SDL_LIB_SHA256  /tmp/sdl-lib.tar.gz" | sha256sum -c - \
    && tar -xzf /tmp/sdl-lib.tar.gz -C /opt/sdl2-miyoo/lib libSDL2-2.0.so.0 libneonarmmiyoo.so \
    && ln -s libSDL2-2.0.so.0 /opt/sdl2-miyoo/lib/libSDL2.so \
    && curl -fsSL -o /tmp/sdl-src.tar.gz "https://github.com/XK9274/sdl2_miyoo/archive/$SDL_COMMIT.tar.gz" \
    && tar -xzf /tmp/sdl-src.tar.gz -C /tmp \
        "sdl2_miyoo-$SDL_COMMIT/include" "sdl2_miyoo-$SDL_COMMIT/LICENSE" "sdl2_miyoo-$SDL_COMMIT/LICENSE.txt" \
    && cp /tmp/sdl2_miyoo-$SDL_COMMIT/include/*.h /opt/sdl2-miyoo/include/SDL2/ \
    && cp /tmp/sdl2_miyoo-$SDL_COMMIT/LICENSE.txt /opt/sdl2-miyoo/share/SDL2-LICENSE.txt \
    && cp /tmp/sdl2_miyoo-$SDL_COMMIT/LICENSE /opt/sdl2-miyoo/share/sdl2_miyoo-LICENSE.txt \
    && rm -rf /tmp/sdl-lib.tar.gz /tmp/sdl-src.tar.gz /tmp/sdl2_miyoo-$SDL_COMMIT

COPY toolchain.cmake /opt/miyoomini-toolchain.cmake
COPY SDL2Config.cmake /opt/sdl2-miyoo/lib/cmake/SDL2/SDL2Config.cmake
