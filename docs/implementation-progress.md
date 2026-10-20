# Implementation progress (handoff)

Handoff for a new session. Read this, then `docs/notes.md` (the reverse-engineering
reference).

## Objective

A personal learning project: reverse engineer **Realms of Chaos** (Apogee, 1995; DOS, Borland
C++ 3.x, VGA Mode X) with Ghidra + GhidraMCP, and port it to run natively on modern systems
(C11 + SDL2, Windows first). It follows the sister projects `../ptomb-port` and
`../monuments-of-mars-port` in layout, tools and approach. The port reimplements the game in C
and reads **all assets at run time from the user's own game files** (`ROC.DAT`, plus a few
tables from `ROC.EXE`). The user is learning, so explain findings as you go.

## Environment (verified)

- Repo: `C:\Users\Jason\Documents\Ghidra\realms-of-chaos-port` (git, branch `main`, no
  remote). `game/`, `ghidra/`, `unpacked/`, `extracted/`, `gfx/`, `build/` and generated
  `analysis/*.tdinfo.txt` are gitignored.
- Build: MSYS2 **mingw64** (gcc, CMake, Ninja, SDL2). With
  `export PATH=/c/msys64/mingw64/bin:$PATH`: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`
  then `cmake --build build`. Warning-free with `-Wall -Wextra -Wpedantic`; keep it so.
- Python: **no system Python**. Use `C:\Users\Jason\anaconda3\envs\ghidra\python.exe -I`.
- Ghidra has `game/ROC.EXE` loaded (it isn't packed) with the Turbo Debugger symbols applied:
  original function and variable names, prototypes, structs and source lines. GhidraMCP's
  backend is plain HTTP at `http://127.0.0.1:8080/`. GhidraMCP can't create functions or run
  scripts. To re-apply the symbols, run `tools/tdinfo.py`, then
  `tools/ghidra_scripts/ApplyTdInfo.java` and `FixDisassembly.java` from the Script Manager,
  then both scripts once more (see notes, "Repairing the disassembly").
- Ghidra 12.1.4 is in `C:\Users\Jason\Downloads\Ghidra\ghidra_12.1.4_PUBLIC` (its JDK is
  beside it). Test Ghidra scripts headless on a *copy* of `ghidra/`: the GUI holds the lock.
- Raw disassembly where Ghidra stops (computed jumps in the blitters): pip-install capstone
  with `--target` into a scratch folder and disassemble the EXE image directly.

## Running

```
build/roc.exe game/ROC.EXE                          # level viewer, level 0
build/roc.exe game/ROC.EXE --level 5 --pos 400,64   # level 5, camera at map pixel 400,64
build/roc.exe game/ROC.EXE --level 5 --screenshot out.png   # one 320x200 view, no window
build/roc.exe game/ROC.EXE --level 5 --no-objects   # the map without its objects
```

`ROC.DAT` must be next to the EXE. Viewer keys: arrows scroll (Shift faster), PgUp / PgDn
change level, O toggles the objects, F12 saves `shotNNN.png`, Esc quits.

## Completed

- Symbols: `tools/tdinfo.py` + `ApplyTdInfo.java` (see notes, "Turbo Debugger symbols").
- Data: `tools/rocdat.py` extracts `ROC.DAT` (LHA); `tools/extract_gfx.py` renders screens,
  sprites, tiles and all 29 level maps to `gfx/`.
- Port, first milestone (level viewer):
  - `src/archive.c`: ROC.DAT index and -lh5- decoder with CRC check.
  - `src/gfx.c`: 8-bit surface; sprite (`_DrawSprite`, with flip), solid tile and masked tile
    blitters.
  - `src/level.c`: the level list (roclevel.txt + the tile-set table at DS:033e in ROC.EXE),
    `.led` loading (palette, maps, tile counts, objects), tile sets, map drawing.
  - `src/png.c`: screenshot writer (from ptomb-port).
  - Verified: a view of every level matches the Python renders pixel for pixel.
- Port, objects: `src/object.c` loads the sprite sets a level's object list names and places
  its objects as LOADOBJ / INITOBJ do (hotspot and the -8 adjustment, the bottom check, first
  frames for the player and level 4's gargoyles, bat facing), drawn as the list drawer at
  3cb4:064c does. Every level loads; spot-checked views show objects standing on the floors.
  They don't move yet: their behaviours (SETUPOBJ / Animator) are the next milestone.
- Ghidra: `FixDisassembly.java` repairs the switch tables Ghidra resolved into the wrong
  segment and the garbage code they caused. It has been applied to the GUI project (ApplyTdInfo,
  FixDisassembly, then both again): 17 switches fixed, 13,137 of 13,138 source lines are code,
  and SETUPOBJ, SETUPHANDLES, GRABSTUFF, OPTIONMENU... decompile properly.

## Next steps

1. **Object behaviours**: SETUPOBJ (RXSETUPF.C) now decompiles. Its switch on the type index
   (notes, "Level objects") gives each object its `Animator`, speeds and frame counts. Start
   with simple animated ones (leaf, bat, bird) to get frame stepping (`ANIMATORUPDATE`,
   `FrameCount` / `MaxFrames` / `AnimInc`) and the object lists (`_MainListHead`,
   `_ForeListHead`, `_SlowListHead`, `_OffListHead`; `MOVEALLOFFTOON`, `SCREENLISTUPDATE`).
   The `Animator` values decompile as odd `DAT_4a77_xxxx` or string addresses: they are near
   pointers into the behaviour code segment, not into DGROUP.
2. **The game loop**: `_main` (RXMAIN.C) → title, menu, `NEWLEVELSETUP` → per-frame
   `PROCESSLEVEL`. Objects are `OBJstruct` (48 bytes) whose `Animator` field is a near pointer
   to a behaviour function (`DOGARGOYLE`, `YOURUN`...). Every behaviour decompiles readably
   now.
3. **Background flags**: bits 9-14 of background cells (probably collision/animation; see
   `CANMOVE`, `RUNANIMTILES`, `ANIMTILE.C`) and the 0x600 bytes at the end of each `.led`.
   GRABSTUFF uses tile behaviour tables at DS:17c0 (background) and DS:15c0 (bonus tiles);
   see notes, "Tile attributes".
4. Parallax backdrop (sky/horizon tiles show through gaps), palettes (`RXPALF.C`), sound
   (Apogee Sound System; VOC effects and MIDI music).
