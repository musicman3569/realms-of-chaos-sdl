#!/bin/sh
# Build dist/RealmsOfChaos-OnionUI.zip, the port for the Miyoo Mini (Plus) under OnionUI, in a
# Docker container (see Dockerfile), from the repository's root:
#
#     packaging/miyoo/build-onion-port.sh
#
# Inside the container it runs itself again with --in-container to cross-compile the port and
# lay it out as OnionUI's Ports Collection does; the zip is extracted to the SD card's root.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
image=roc-miyoo-build

if [ "${1:-}" != --in-container ]; then
    docker build --network host -t "$image" "$here"
    exec docker run --rm --network none --user "$(id -u):$(id -g)" -e HOME=/tmp \
        -v "$root:/src" -w /src "$image" /src/packaging/miyoo/build-onion-port.sh --in-container
fi

build=/src/build/miyoo
stage=$build/package
name="Realms of Chaos"
ports=$stage/Roms/PORTS
game=$ports/Games/$name
rm -rf "$stage"

cmake -S /src -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DROC_MIYOO=ON \
    -DCMAKE_TOOLCHAIN_FILE=/opt/miyoomini-toolchain.cmake -DSDL2_DIR=/opt/sdl2-miyoo/lib/cmake/SDL2 \
    -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON -DCMAKE_INSTALL_RPATH='$ORIGIN/lib'
cmake --build "$build"

mkdir -p "$game/lib" "$ports/Shortcuts/Platform" "$ports/Imgs"
cp "$build/roc" "$game/"
/opt/miyoomini-toolchain/usr/bin/arm-linux-gnueabihf-strip "$game/roc"
cp /opt/sdl2-miyoo/lib/libSDL2-2.0.so.0 /opt/sdl2-miyoo/lib/libneonarmmiyoo.so \
    /opt/sdl2-miyoo/share/*.txt "$game/lib/"
cp "$here/port/README.txt" "$here/port/_required_files.txt" "$here/port/PLACE ROC.EXE AND ROC.DAT HERE.txt" "$game/"
# shipped as .notfound, as the Ports Collection does: Ports' "~Import ports" makes it a .port
# once ROC.DAT is in the game's folder
cp "$here/port/$name.port" "$ports/Shortcuts/Platform/$name.notfound"
# the list's picture: the title screen, at the Ports Collection's 250 pixels wide
convert /src/docs/screenshots/title.png -resize 250x156 "$ports/Imgs/$name.png"
cp /src/LICENSE "$game/LICENSE.txt"

file "$game/roc"
mkdir -p /src/dist
rm -f /src/dist/RealmsOfChaos-OnionUI.zip
(cd "$stage" && zip -qr /src/dist/RealmsOfChaos-OnionUI.zip Roms)
echo "built dist/RealmsOfChaos-OnionUI.zip"
