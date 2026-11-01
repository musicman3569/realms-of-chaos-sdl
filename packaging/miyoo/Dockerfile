# The Miyoo Mini (Plus) build environment: the cross toolchain shauninman's
# union-miyoomini-toolchain installs (GCC 8.3 for arm-linux-gnueabihf, glibc 2.28, the vendor's
# MI libraries), in Ubuntu 22.04 for a CMake new enough for CMakeLists.txt; and SDL2 for the
# handheld: XK9274's sdl2_miyoo (SDL 2.0.20 with the MI_GFX / MI_AO drivers, as the Ports
# Collection's newer SDL2 ports bundle it), its prebuilt library and the headers of the same commit,
# kept in sdl2-miyoo/ since the upstream repository is gone (see sdl2-miyoo/README.md).
FROM ubuntu:22.04

ARG DEBIAN_FRONTEND=noninteractive
ARG TOOLCHAIN_URL=https://github.com/shauninman/miyoomini-toolchain-buildroot/releases/download/v0.0.3/miyoomini-toolchain.tar.xz
ARG TOOLCHAIN_SHA256=e158b6ebde0c933ecd57b36760fb8602fad4176a09fb61559dc7bcffe6624f2e

RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake ninja-build ca-certificates curl xz-utils file zip imagemagick \
    && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL -o /tmp/toolchain.tar.xz "$TOOLCHAIN_URL" \
    && echo "$TOOLCHAIN_SHA256  /tmp/toolchain.tar.xz" | sha256sum -c - \
    && tar -xJf /tmp/toolchain.tar.xz -C /opt \
    && rm /tmp/toolchain.tar.xz

COPY sdl2-miyoo/SHA256SUMS /opt/sdl2-miyoo/
COPY sdl2-miyoo/lib /opt/sdl2-miyoo/lib
COPY sdl2-miyoo/include /opt/sdl2-miyoo/include
COPY sdl2-miyoo/share /opt/sdl2-miyoo/share
RUN cd /opt/sdl2-miyoo && sha256sum -c --quiet SHA256SUMS \
    && ln -s libSDL2-2.0.so.0 lib/libSDL2.so

COPY toolchain.cmake /opt/miyoomini-toolchain.cmake
COPY SDL2Config.cmake /opt/sdl2-miyoo/lib/cmake/SDL2/SDL2Config.cmake
