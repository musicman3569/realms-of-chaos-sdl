#!/usr/bin/env python3
"""List or extract ROC.DAT, the game's LHA archive of sounds, sprites, levels and music.

Usage: python rocdat.py ROC.DAT                 list the files
       python rocdat.py ROC.DAT OUTPUT_DIR      extract them all (CRC-checked)
       python rocdat.py ROC.DAT OUTPUT_DIR NAME...   extract only the named files

ROC.DAT is a plain LHA archive: level-1 headers, files compressed with -lh5- (a few stored
with -lh0-), one after another, terminated by a zero byte. The game reads it with Haruhiko
Okumura's ar002 decoder (HUF.C, DECODE.C, MAKETBL.C, with AREAD in UAR.C), which this
script reimplements: LZSS with an 8 KB window whose literals/lengths and distances are
Huffman coded in blocks.

Read-only for the archive; it only writes into OUTPUT_DIR.
"""

import argparse
import os
import struct
import sys

DICBIT = 13          # 8 KB window
NC = 256 + 256 - 3 + 2   # 510: literals + match lengths 3..256 (+1 unused)
NP = DICBIT + 1      # distance codes
NT = 19              # code-length codes
TBIT, CBIT, PBIT = 5, 9, 4
THRESHOLD = 3


def crc16(data):
    """CRC-16/ARC as LHA uses it (poly 0xA001 reflected, initial value 0)."""
    table = crc16.table
    crc = 0
    for b in data:
        crc = (crc >> 8) ^ table[(crc ^ b) & 0xFF]
    return crc


crc16.table = []
for _i in range(256):
    _c = _i
    for _ in range(8):
        _c = (_c >> 1) ^ 0xA001 if _c & 1 else _c >> 1
    crc16.table.append(_c)


class BitReader:
    def __init__(self, data):
        self.data = data
        self.pos = 0
        self.buf = 0     # bits not consumed yet, MSB first
        self.count = 0   # number of bits in buf

    def _fill(self, n):
        while self.count < n:
            b = self.data[self.pos] if self.pos < len(self.data) else 0
            self.pos += 1
            self.buf = (self.buf << 8) | b
            self.count += 8

    def peek(self, n):
        self._fill(n)
        return (self.buf >> (self.count - n)) & ((1 << n) - 1)

    def skip(self, n):
        self._fill(n)
        self.count -= n
        self.buf &= (1 << self.count) - 1

    def get(self, n):
        v = self.peek(n)
        self.skip(n)
        return v


class Huffman:
    """Canonical Huffman decoder using a 16-bit lookup table."""

    def __init__(self, lengths, single=None):
        self.single = single
        if single is not None:
            return
        self.table = [None] * 65536
        code = 0
        for length in range(1, 17):
            for sym, l in enumerate(lengths):
                if l == length:
                    start = code << (16 - length)
                    end = (code + 1) << (16 - length)
                    if end > 65536:
                        raise ValueError("bad Huffman table")
                    self.table[start:end] = [(sym, length)] * (end - start)
                    code += 1
            code <<= 1

    def decode(self, bits):
        if self.single is not None:
            return self.single
        entry = self.table[bits.peek(16)]
        if entry is None:
            raise ValueError("bad Huffman code")
        sym, length = entry
        bits.skip(length)
        return sym


def read_pt_len(bits, nn, nbit, special):
    n = bits.get(nbit)
    if n == 0:
        return Huffman(None, single=bits.get(nbit))
    lengths = [0] * nn
    i = 0
    while i < n:
        c = bits.get(3)
        if c == 7:
            while bits.get(1):
                c += 1
        lengths[i] = c
        i += 1
        if i == special:
            for _ in range(bits.get(2)):
                lengths[i] = 0
                i += 1
    return Huffman(lengths)


def read_c_len(bits, pt):
    n = bits.get(CBIT)
    if n == 0:
        return Huffman(None, single=bits.get(CBIT))
    lengths = [0] * NC
    i = 0
    while i < n:
        c = pt.decode(bits)
        if c <= 2:
            run = 1 if c == 0 else bits.get(4) + 3 if c == 1 else bits.get(CBIT) + 20
            i += run
        else:
            lengths[i] = c - 2
            i += 1
    return Huffman(lengths)


def lh5_decode(data, size):
    bits = BitReader(data)
    out = bytearray()
    block = 0
    while len(out) < size:
        if block == 0:
            block = bits.get(16)
            pt = read_pt_len(bits, NT, TBIT, 3)
            c_tab = read_c_len(bits, pt)
            p_tab = read_pt_len(bits, NP, PBIT, -1)
        block -= 1
        c = c_tab.decode(bits)
        if c < 256:
            out.append(c)
            continue
        length = c - 256 + THRESHOLD
        p = p_tab.decode(bits)
        if p:
            p = (1 << (p - 1)) + bits.get(p - 1)
        src = len(out) - p - 1
        if src < 0:
            raise ValueError("match before start of file")
        for k in range(length):
            out.append(out[src + k])
    return bytes(out[:size])


def entries(arc):
    """Yield (name, method, packed data, original size, crc) for each archived file."""
    p = 0
    while p < len(arc) and arc[p]:
        size = arc[p]
        method = arc[p + 2:p + 7].decode("latin-1")
        packed, orig = struct.unpack_from("<II", arc, p + 7)
        level = arc[p + 20]
        if level != 1:
            raise ValueError(f"unsupported LHA header level {level} at {p}")
        nlen = arc[p + 21]
        name = arc[p + 22:p + 22 + nlen].decode("latin-1")
        crc = struct.unpack_from("<H", arc, p + 22 + nlen)[0]
        q = p + size + 2
        ext = struct.unpack_from("<H", arc, q - 2)[0]
        while ext:  # extended headers count towards the packed size in level 1
            packed -= ext
            nxt = struct.unpack_from("<H", arc, q + ext - 2)[0]
            q += ext
            ext = nxt
        yield name, method, arc[q:q + packed], orig, crc
        p = q + packed


def unpack(method, data, orig):
    if method == "-lh0-":
        return data
    if method == "-lh5-":
        return lh5_decode(data, orig)
    raise ValueError(f"unsupported method {method}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("archive")
    ap.add_argument("output", nargs="?")
    ap.add_argument("names", nargs="*")
    args = ap.parse_args()

    with open(args.archive, "rb") as f:
        arc = f.read()
    wanted = {n.lower() for n in args.names}
    count = total = bad = 0
    if args.output:
        os.makedirs(args.output, exist_ok=True)
    for name, method, data, orig, crc in entries(arc):
        if wanted and name.lower() not in wanted:
            continue
        count += 1
        total += orig
        if not args.output:
            print(f"{name:16} {method} {len(data):8} {orig:8}")
            continue
        out = unpack(method, data, orig)
        if crc16(out) != crc:
            print(f"{name}: CRC mismatch", file=sys.stderr)
            bad += 1
        with open(os.path.join(args.output, name), "wb") as f:
            f.write(out)
    action = f"extracted to {args.output}" if args.output else "listed"
    print(f"{count} files, {total} bytes {action}" + (f", {bad} CRC errors" if bad else ""))
    if bad:
        sys.exit(1)


if __name__ == "__main__":
    main()
