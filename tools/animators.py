#!/usr/bin/env python3
"""List the Animator (behaviour function) SETUPOBJ gives each object type.

Usage: python animators.py game/ROC.EXE analysis/ROC.tdinfo.txt

INITOBJ / SPAWNOBJ put an object type index in UserPtr->FrameNo and call SETUPOBJ
(RXSETUPF.C, 2a35:0004), whose switch (96 cases, table at 2a35:154a) sets the object's
fields. The Animator is a near pointer (OBJstruct offset 0x2c) into code segment 1000:
ANIMATORUPDATE (1000:c268) calls it with a near CALL. This scans each case's code for
"MOV word ptr [SI+2Ch], imm16" (C7 44 2C lo hi) up to the next case and names the target
from the Turbo Debugger export. Several cases pick one of a few animators by level or by the
object's info string; all of them are listed.
"""

import bisect
import struct
import sys

SETUPOBJ_SEG = 0x2A35
TABLE = 0x154A
CASES = 96
ANIM_SEG = 0x1000


def main():
    exe_path, td_path = sys.argv[1], sys.argv[2]
    exe = open(exe_path, "rb").read()
    img = exe[struct.unpack_from("<H", exe, 8)[0] * 16:]

    def lin(seg, off):
        return (seg - 0x1000) * 16 + off

    names = {}
    for line in open(td_path):
        f = line.rstrip("\n").split("\t")
        if f[0] in ("FUNC", "CODE") and f[1].startswith("1000:"):
            names[int(f[1][5:], 16)] = f[2]
    starts = sorted(names)

    def name(off):
        i = bisect.bisect_right(starts, off) - 1
        if i < 0:
            return "?"
        base = starts[i]
        return names[base] if base == off else f"{names[base]}+{off - base:x}"

    targets = [struct.unpack_from("<H", img, lin(SETUPOBJ_SEG, TABLE + 2 * k))[0]
               for k in range(CASES)]
    ordered = sorted(set(targets)) + [TABLE]
    for k, t in enumerate(targets):
        end = ordered[ordered.index(t) + 1]
        code = img[lin(SETUPOBJ_SEG, t):lin(SETUPOBJ_SEG, end)]
        found = []
        p = code.find(b"\xc7\x44\x2c")
        while p >= 0:
            v = struct.unpack_from("<H", code, p + 3)[0]
            found.append(f"{v:04x} {name(v)}")
            p = code.find(b"\xc7\x44\x2c", p + 1)
        print(f"{k:02x}  case {SETUPOBJ_SEG:04x}:{t:04x}  " + (", ".join(found) or "-"))


if __name__ == "__main__":
    main()
