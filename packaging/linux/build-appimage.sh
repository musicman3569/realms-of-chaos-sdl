#!/bin/sh
# Build dist/RealmsOfChaos-x86_64.AppImage in a Docker container (see Dockerfile), from the
# repository's root:
#
#     packaging/linux/build-appimage.sh
#
# Inside the container it runs itself again with --in-container to build the port, put it in an
# AppDir with SDL2 beside it, and pack that.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
image=roc-appimage-build

if [ "${1:-}" != --in-container ]; then
    docker build --network host -t "$image" "$here"
    exec docker run --rm --network none --user "$(id -u):$(id -g)" -e HOME=/tmp \
        -v "$root:/src" -w /src "$image" /src/packaging/linux/build-appimage.sh --in-container
fi

build=/src/build/appimage
appdir=$build/AppDir
rm -rf "$appdir"

cmake -S /src -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/sdl2 \
    -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_RPATH='$ORIGIN/../lib'
cmake --build "$build"
DESTDIR="$appdir" cmake --install "$build" --strip

mkdir -p "$appdir/usr/lib"
cp -L /opt/sdl2/lib/libSDL2-2.0.so.0 "$appdir/usr/lib/"
strip --strip-unneeded "$appdir/usr/lib/libSDL2-2.0.so.0"
cp "$here/realms-of-chaos.desktop" "$here/realms-of-chaos.svg" "$appdir/"
ln -s usr/bin/roc "$appdir/AppRun"

mkdir -p /src/dist
ARCH=x86_64 /opt/appimagetool.AppImage --no-appstream --runtime-file /opt/runtime-x86_64 "$appdir" /src/dist/RealmsOfChaos-x86_64.AppImage
