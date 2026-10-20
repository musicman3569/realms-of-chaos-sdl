#!/usr/bin/env python3
"""Render Realms of Chaos graphics (screens, sprites, tiles, whole levels) to PNG.

Usage: python extract_gfx.py EXTRACTED_DIR OUTPUT_DIR [--exe game/ROC.EXE] [--zoom N]

EXTRACTED_DIR is the output of tools/rocdat.py. Writes:
  screens/NAME.png   the .vga full screens
  sprites/NAME.png   every frame of each .spr, left to right (transparent background)
  tiles/SET.png      the background tiles of each .bg, 20 per row as the game stores them
  levels/NN_NAME.png every level map: background tiles with the foreground/bonus tiles over them
Read-only for the game files.

Formats (from LOADSPRITE, LOADLEVEL and the DRW*.ASM blitters in ROC.EXE):

  .vga  768-byte palette (6-bit VGA components), then 320x200 pixels.
  .spr  21-byte header (file name, ...), then per frame: u16 size + a SPRITE struct: Rows,
        Cols, ImgXoff, ImgYoff, BoxCols, BoxRows, HitCols, HitRows, HitXoff, HitYoff,
        u16 PostOff, 6 bytes of EMS/XMS bookkeeping, a fill byte, then Data. Data is a
        column-major stream of (op, count) byte pairs: 0 skip count rows (transparent),
        1 copy count pixels downwards, 2 next column, 3 end. A u16 per column (the start of
        each column in Data, for clipping) follows at Data+PostOff.
  .bg   16x16 tiles of 256 bytes in Mode X order: four 64-byte planes, each plane 4 pixels
        wide (pixel x of plane p is x*4+p). Background layer 1 tiles, then layers 2 and 3.
  .mfg/.mbg  foreground / "bonus" masked tiles: 128 u16 offsets, u16 size, then the data.
        Each tile is a stream of (op, count): 0 skip count rows, 1 count rows of two bytes,
        2 / 3 count rows of only the first / second byte (each reads a word), 4 next plane
        (after plane 3, the right half of the tile), 5 end. The left half is plane bytes 0-1
        (pixels 0-7), the right half bytes 2-3.
  .led  768-byte palette, u8 Rows, u8 Cols, Rows*Cols u16 background map, the foreground map
        (3 header bytes + Rows*Cols bytes), five u16 tile counts (bg1, bg2, bg3, fg, bonus),
        u16 object count + 62-byte objects (SPRITE_LIST: name, info, x, y), then tables.
        Background cell: tile = value & 0x1ff (0-239 layer 1, 240-479 layer 2, 480- layer 3);
        the upper bits are flags. Foreground cell: 0 = none, else low 7 bits pick a tile from
        .mfg if bit 7 is set, or from .mbg if not.
Level n is <name[n]>.led drawn with the tile set <name[set[n]]>.bg/.mfg/.mbg, where name[]
is roclevel.txt (9-character slots) and set[] is a 29-byte table at DGROUP:033e in ROC.EXE.
"""

import argparse
import os
import struct
import zlib

DGROUP = 0x4A77 - 0x1000       # paragraph of DGROUP within the load image
LEVEL_SET_TABLE = 0x033E
LEVELS = 29


def write_png(path, width, height, rgba):
    """rgba: bytes of width*height*4."""
    stride = width * 4
    raw = b"".join(b"\x00" + rgba[y * stride:(y + 1) * stride] for y in range(height))

    def chunk(kind, data):
        c = kind + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 6)))
        f.write(chunk(b"IEND", b""))


class Canvas:
    """Palette-indexed image; None = transparent."""

    def __init__(self, width, height):
        self.w, self.h = width, height
        self.px = [None] * (width * height)

    def put(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y * self.w + x] = c

    def blit(self, x0, y0, pixels):
        for (x, y), c in pixels.items():
            self.put(x0 + x, y0 + y, c)

    def save(self, path, palette, zoom=1):
        rgb = [bytes((palette[i * 3] * 255 // 63, palette[i * 3 + 1] * 255 // 63,
                      palette[i * 3 + 2] * 255 // 63, 255)) for i in range(256)]
        clear = b"\x00\x00\x00\x00"
        out = bytearray()
        for y in range(self.h):
            row = b"".join((rgb[c] if c is not None else clear) * zoom
                           for c in self.px[y * self.w:(y + 1) * self.w])
            out += row * zoom
        write_png(path, self.w * zoom, self.h * zoom, bytes(out))


# ---- decoders: each returns {(x, y): colour} ----

def decode_vga(data):
    pal, pix = data[:768], data[768:768 + 64000]
    return pal, {(i % 320, i // 320): c for i, c in enumerate(pix)}


def sprite_frames(data):
    p = 21
    while p + 2 <= len(data):
        size = struct.unpack_from("<H", data, p)[0]
        yield data[p + 2:p + 2 + size]
        p += 2 + size


def decode_sprite(s):
    rows, cols = s[0], s[1]
    pixels = {}
    q, x, y = 19, 0, 0
    while q + 2 <= len(s):
        op, count = s[q], s[q + 1]
        q += 2
        if op == 0:
            y += count
        elif op == 1:
            for k in range(count):
                pixels[(x, y + k)] = s[q + k]
            q += count
            y += count
        elif op == 2:
            x, y = x + 1, 0
        else:
            break
    return cols, rows, pixels


def decode_tile(t):
    return {(x * 4 + p, y): t[p * 64 + y * 4 + x] for p in range(4) for y in range(16)
            for x in range(4)}


def decode_mask_tile(data, start):
    pixels = {}
    q, plane, half, y = start, 0, 0, 0
    while q + 2 <= len(data):
        op, count = data[q], data[q + 1]
        q += 2
        if op == 0:
            y += count
        elif op in (1, 2, 3):
            for _ in range(count):
                b0, b1 = data[q], data[q + 1]
                q += 2
                if op in (1, 2):
                    pixels[((half * 2) * 4 + plane, y)] = b0
                if op in (1, 3):
                    pixels[((half * 2 + 1) * 4 + plane, y)] = b1
                y += 1
        elif op == 4:
            plane += 1
            if plane == 4:
                plane, half = 0, half + 1
            y = 0
        else:
            break
    return pixels


def load_masked(path):
    with open(path, "rb") as f:
        d = f.read()
    offsets = struct.unpack_from("<128H", d, 0)
    data = d[258:]
    return [decode_mask_tile(data, o) for o in offsets]


def load_level(path):
    with open(path, "rb") as f:
        d = f.read()
    pal = d[:768]
    rows, cols = d[768], d[769]
    p = 770
    bg = struct.unpack_from(f"<{rows * cols}H", d, p)
    p += rows * cols * 2
    fg = d[p + 3:p + 3 + rows * cols]
    return pal, rows, cols, bg, fg


# ---- outputs ----

def render_screens(src, out, zoom):
    os.makedirs(out, exist_ok=True)
    for name in sorted(f for f in os.listdir(src) if f.endswith(".vga")):
        with open(os.path.join(src, name), "rb") as f:
            pal, pixels = decode_vga(f.read())
        c = Canvas(320, 200)
        c.blit(0, 0, pixels)
        c.save(os.path.join(out, name[:-4] + ".png"), pal, zoom)


def render_sprites(src, out, palette, zoom):
    os.makedirs(out, exist_ok=True)
    for name in sorted(f for f in os.listdir(src) if f.endswith(".spr")):
        with open(os.path.join(src, name), "rb") as f:
            frames = [decode_sprite(s) for s in sprite_frames(f.read())]
        if not frames:
            continue
        cell_w = max(w for w, _, _ in frames) + 1
        cell_h = max(h for _, h, _ in frames)
        per_row = max(1, min(len(frames), 1024 // cell_w))
        nrows = (len(frames) + per_row - 1) // per_row
        c = Canvas(cell_w * per_row, cell_h * nrows + nrows - 1)
        for i, (_, _, pixels) in enumerate(frames):
            c.blit((i % per_row) * cell_w, (i // per_row) * (cell_h + 1), pixels)
        c.save(os.path.join(out, name[:-4] + ".png"), palette, zoom)


def level_sets(exe_path, src):
    with open(exe_path, "rb") as f:
        exe = f.read()
    image = exe[struct.unpack_from("<H", exe, 8)[0] * 16:]
    sets = image[DGROUP * 16 + LEVEL_SET_TABLE:DGROUP * 16 + LEVEL_SET_TABLE + LEVELS]
    with open(os.path.join(src, "roclevel.txt"), "rb") as f:
        text = f.read()
    names = [text[i * 9:i * 9 + 9].decode("latin-1").strip(" \0") for i in range(LEVELS)]
    return [(i, names[i], names[sets[i]]) for i in range(LEVELS)]


def render_tiles(src, out, sets, zoom):
    os.makedirs(out, exist_ok=True)
    palettes = {}
    for _, name, tset in sets:
        led = os.path.join(src, name + ".led")
        if os.path.exists(led):
            palettes.setdefault(tset, load_level(led)[0])
    for tset, pal in palettes.items():
        path = os.path.join(src, tset + ".bg")
        if not os.path.exists(path):
            continue
        with open(path, "rb") as f:
            bg = f.read()
        n = len(bg) // 256
        c = Canvas(20 * 17 - 1, ((n + 19) // 20) * 17 - 1)
        for t in range(n):
            c.blit((t % 20) * 17, (t // 20) * 17, decode_tile(bg[t * 256:(t + 1) * 256]))
        c.save(os.path.join(out, tset + ".png"), pal, zoom)


def render_levels(src, out, sets, zoom):
    os.makedirs(out, exist_ok=True)
    for i, name, tset in sets:
        led = os.path.join(src, name + ".led")
        if not os.path.exists(led) or not os.path.exists(os.path.join(src, tset + ".bg")):
            continue
        pal, rows, cols, bgmap, fgmap = load_level(led)
        with open(os.path.join(src, tset + ".bg"), "rb") as f:
            bg = f.read()
        tiles = {}
        fg_tiles = load_masked(os.path.join(src, tset + ".mfg"))
        bon_tiles = load_masked(os.path.join(src, tset + ".mbg"))
        c = Canvas(cols * 16, rows * 16)
        for r in range(rows):
            for col in range(cols):
                t = bgmap[r * cols + col] & 0x1FF
                if t not in tiles:
                    tiles[t] = decode_tile(bg[t * 256:(t + 1) * 256])
                c.blit(col * 16, r * 16, tiles[t])
                f = fgmap[r * cols + col]
                if f & 0x7F:
                    c.blit(col * 16, r * 16, (fg_tiles if f & 0x80 else bon_tiles)[f & 0x7F])
        c.save(os.path.join(out, f"{i:02d}_{name}.png"), pal, zoom)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("extracted")
    ap.add_argument("output")
    ap.add_argument("--exe", default="game/ROC.EXE")
    ap.add_argument("--zoom", type=int, default=1)
    ap.add_argument("--only", choices=["screens", "sprites", "tiles", "levels"])
    args = ap.parse_args()

    sets = level_sets(args.exe, args.extracted)
    sprite_palette = load_level(os.path.join(args.extracted, "forest1.led"))[0]
    jobs = {
        "screens": lambda: render_screens(args.extracted, os.path.join(args.output, "screens"),
                                          args.zoom),
        "sprites": lambda: render_sprites(args.extracted, os.path.join(args.output, "sprites"),
                                          sprite_palette, args.zoom),
        "tiles": lambda: render_tiles(args.extracted, os.path.join(args.output, "tiles"), sets,
                                      args.zoom),
        "levels": lambda: render_levels(args.extracted, os.path.join(args.output, "levels"),
                                        sets, args.zoom),
    }
    for name, job in jobs.items():
        if args.only in (None, name):
            job()
            print(f"{name} -> {os.path.join(args.output, name)}")


if __name__ == "__main__":
    main()
