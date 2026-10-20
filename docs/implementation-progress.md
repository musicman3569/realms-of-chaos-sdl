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
build/roc.exe game/ROC.EXE                          # run level 0
build/roc.exe game/ROC.EXE --level 5 --pos 400,64   # level 5, free camera at map pixel 400,64
build/roc.exe game/ROC.EXE --level 5 --no-objects   # the map without its objects
# headless: run 1800 ticks (about 3 s) with the camera and the player placed, save one view
build/roc.exe game/ROC.EXE --level 0 --pos 620,100 --player 770,250 --ticks 1800 --screenshot out.png
```

`ROC.DAT` must be next to the EXE. The level runs at the game's 582 Hz tick rate. The
player's behaviour isn't ported, so the arrow keys move the player freely (Shift faster) and
the view follows; C toggles a free camera, PgUp / PgDn change level, O toggles the objects,
Space pauses, F12 saves `shotNNN.png`, Esc quits. Objects whose behaviour isn't ported stay
still, and each such animator is reported once on stderr.

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
- Port, object engine (notes, "Running objects" and "Tiles"):
  - `src/engine.c`: objects (OBJstruct + USERstruct, 16-bit arithmetic), the priority-sorted
    lists and the off-screen list, SPAWNOBJ / OBJKILL, ANIMATORUPDATE at 582 ticks a
    second, NEXTFRAME, OBJVISIBLE / OBJONSCREEN, CANGO* / CANMOVE (collision bits, tile
    types, slopes), Borland `rand`, drawing. Animators keep their original 16-bit values and
    are looked up in a table; unported ones do nothing. Sound, damage and the player are
    stubs.
  - `src/setup.c`: INITOBJ, and SETUPOBJ for the 40 object types the levels place plus the
    spawned leaf, bird and dropping.
  - `src/behave.c`: DOLEAF/FDOLEAF, DOBIRD, DOBAT, DOARROWSIGN, GENERICBEGINFALL,
    GENERICFALL, GENERICDOWN, DOPROJECTILE (the droppings' path), NULLANIMATOR.
  - `src/level.c` also loads the tile type tables and `_slope`; `src/imagetab.h` (generated
    by `tools/gen_imagetab.py`) is the `_Img` layout.
  - Verified headless: all 29 levels run 30,000 ticks; in forest1 the bats fly in from the
    view's edge, bob and bounce between the edges, the birds take off when the player comes
    near and their droppings fall, land and splat, and the leaves fall.
- Ghidra: `FixDisassembly.java` repairs the switch tables Ghidra resolved into the wrong
  segment and the garbage code they caused. It has been applied to the GUI project (ApplyTdInfo,
  FixDisassembly, then both again): 17 switches fixed, 13,137 of 13,138 source lines are code,
  and SETUPOBJ, SETUPHANDLES, GRABSTUFF, OPTIONMENU... decompile properly.

## Next steps

1. **More behaviours**, a module (RXFOREST.C, RXCASTLE.C, ...) at a time; stderr lists the
   animators a level reaches that aren't ported. Forest1 needs BANDITWALK (cat bandits),
   DOCONTAINER (statues: hints and gems), FLOATINGPLATFORM / CRUMBLINGPLATFORM (catwalk), the
   bat's HITPLAYER path, DOCAVESPIKES (stakes). Then the remaining SETUPOBJ cases for spawned
   types. `tools/animators.py` maps types to animators.
2. **The player** (RXYOU.C, RXYOUF.C: YOUBEGINRUN and the YOU* states), with keyboard input
   (KBD3.C), the scrolling (`SCROLLRIGHT`... in 3cb4, `PROCESSLEVEL`) and damage (HITPLAYER,
   ENEMYHIT). This replaces the viewer's free-flying player and centred camera.
3. **The game loop**: `_main` (RXMAIN.C) → title, menu, `NEWLEVELSETUP` → per-frame
   `PROCESSLEVEL`. Objects are `OBJstruct` (48 bytes) whose `Animator` field is a near pointer
   to a behaviour function (`DOGARGOYLE`, `YOURUN`...). Every behaviour decompiles readably
   now.
4. **Background flags**: bits 9-14 of background cells (probably collision/animation; see
   `RUNANIMTILES`, `ANIMTILE.C`) and the 0x600 bytes at the end of each `.led`. Bits 9-12
   are the collision flags (notes, "Tiles"); 13 is GRABSTUFF's secret bit.
5. Parallax backdrop (sky/horizon tiles show through gaps), palettes (`RXPALF.C`), sound
   (Apogee Sound System; VOC effects and MIDI music).
