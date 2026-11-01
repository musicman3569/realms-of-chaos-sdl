# sdl2_miyoo (XK9274), prebuilt: a preserved copy

Vendored from the private repository musicman3569/sdl2-miyoo-xk9274 (commit `020787b3b95c`). The
Dockerfile copies these files into the build image and checks them against `SHA256SUMS`.

SDL 2.0.20 with drivers for the Miyoo Mini (Plus)'s display (MI_GFX), sound (MI_AO) and
buttons (the `mmiyoo` video and audio drivers), from XK9274's
[sdl2_miyoo](https://github.com/XK9274/sdl2_miyoo). That repository, its releases and the
account are no longer online (they returned 404 on 2026-10-31).

**This is not the source code.** It holds only the build that the Commander Genius and
Realms of Chaos OnionUI ports are built against:

| Path | What |
|---|---|
| `lib/libSDL2-2.0.so.0`, `lib/libneonarmmiyoo.so` | from release `sdl2-miyoo-178fc0fc`, asset `libSDL2-miyoo-nogl-178fc0fc.tar.gz` (SHA-256 of that tarball: `e2c78624c88b117e0613a49de00634b001287942357fd58606d2a0f249778b8e`) |
| `include/SDL2/` | the headers of commit `178fc0fcfbdc8a92fbac16171fe945eb66d8f7d5` |
| `share/SDL2-LICENSE.txt` | SDL's license (zlib) |
| `share/sdl2_miyoo-LICENSE.txt` | sdl2_miyoo's license (GPL-3.0, also in `LICENSE`) |
| `SHA256SUMS` | checksums of all of the above (`sha256sum -c SHA256SUMS`) |

The files were downloaded and checksum-verified on 2026-10-25 by the realms-of-chaos-sdl Miyoo
build image, and copied out of that image on 2026-10-31. The tarball itself wasn't kept, so
its checksum above can't be re-checked; the files' own checksums are in `SHA256SUMS`.

## Source

The matching source (commit `178fc0fc`) couldn't be found: Software Heritage never archived
the repository, and the surviving mirrors don't contain that commit. The nearest available
source is the older code in [S3ggie/sdl2_miyoo](https://github.com/S3ggie/sdl2_miyoo) (newest
commit January 2024). sdl2_miyoo is GPL-3.0, so if you redistribute these binaries, keep in
mind that the license expects the corresponding source to be offered with them.

## Using it

Both libraries go into the port's `lib/` folder (on the program's `LD_LIBRARY_PATH` or
`$ORIGIN/lib` rpath). `libSDL2-2.0.so.0` also needs the vendor's `libcam_os_wrapper.so` and
`libmi_*` libraries, which only the handheld has (in `/config/lib`). Its newest glibc symbol is
2.28, matching the handheld.

- `SDL_VIDEODRIVER=mmiyoo`, `SDL_AUDIODRIVER=mmiyoo`.
- `SDL_MMIYOO_INPUT_MODE`: unset, the buttons are a game controller; `keyboard` sends them as
  the keys OnionUI's ports use: D-pad arrows, A Space, B Left Ctrl, X Left Shift, Y Left Alt,
  L1 E, R1 T, L2 Tab, R2 Backspace, Select Right Ctrl, Start Return, Menu Escape.
- Debugging: `SDL_MMIYOO_DEBUG`, `SDL_MMIYOO_DEBUG_VERBOSE`, `SDL_MMIYOO_GEOMETRY_STATS`.
