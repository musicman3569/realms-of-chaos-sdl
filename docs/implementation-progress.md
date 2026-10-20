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
build/roc.exe game/ROC.EXE                          # play level 0
build/roc.exe game/ROC.EXE --level 5                # play level 5
# headless: 1800 ticks (about 3 s) with the camera fixed and the player placed; save the view
build/roc.exe game/ROC.EXE --level 0 --pos 620,100 --player 770,250 --ticks 1800 --screenshot out.png
# headless with scripted keys: walk right, jump at ticks 1000-1100
build/roc.exe game/ROC.EXE --level 0 --ticks 3000 --hold right:0-3000,jump:1000-1100 --screenshot out.png
# headless test: move the player across the whole map; list every object at the end
build/roc.exe game/ROC.EXE --level 1 --ticks 40000 --sweep --list --screenshot out.png
```

`ROC.DAT` must be next to the EXE. The level runs at the game's 582 Hz tick rate. Game keys:
arrows, Ctrl jump, Alt attack (not ported yet), Enter + up / down look. Viewer keys: [ and ]
change level, C free camera (the arrows move the view), O hides the objects, P pauses, F12
saves `shotNNN.png`, Esc quits. Falling into a pit restarts the level (until death is
ported). Objects whose behaviour isn't ported stay still, and each such animator is reported
once on stderr. Headless runs print the player's position and animator at the end.

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
- Port, forest behaviours: the behaviours are split by original module (`src/rxgenerl.c`,
  `rxforest.c`, `rxplat.c`, `rxcave.c`; `behave.c` is the table). Ported: BANDITWALK, WOLFRUN,
  DOCAVESPIKES (stakes), DOCONTAINER (statues, except being smashed), the platforms
  (FLOATINGPLATFORM, CRUMBLINGPLATFORM + shake / fall, pulley platforms + DOPULLEY, the dip)
  with MAINTAINPLATFORM carrying the player, and the generic jump (GENERICBEGINJUMP,
  GENERICJUMP, GENERICUP). Every object in the three forest levels now runs its own
  behaviour; the player's and the damage it takes are the stubs left. Verified headless with
  `--list`: bandits walk at 32 px/s, follow slopes and turn at ledges and every 0x835 ticks;
  wolves run and turn; stakes cycle; floating platforms travel their 112 px and carry the
  player; the player rides a pulley platform its 144 steps; a crumbling platform shakes,
  falls and breaks up. A `--sweep` of all 29 levels runs without errors.
- Port, the player (`src/rxyou.c`, notes "The player"): standing, walking (following
  slopes), the idle animations, jumping (held for height), falling, the stun after a long
  fall, crouching and crawling, climbing vines and ropes, scrambling over vine tops; keyboard
  input through `_Keys` / `_KeyTrans`; the real PLAYER* moves and scrolling (dead zone,
  SCROLL*, the SCREENCENTER camera object), with the starting view as NEWLEVEL sets it.
  Verified headless with `--hold`: walking at 106 px/s over slopes with the view scrolling,
  a jump of 50 pixels, crouch / crawl / stand, a 179-pixel fall ending in a 180-tick stun,
  climbing a castle rope (44 px/s), climbing to a vine top in dead2 and standing on the
  ledge, riding a pulley platform. Not yet: attacking (whip / fireballs), the special item,
  damage and death (HITPLAYER, YOUHIT, YOUDIE...), GRABSTUFF (gems, secrets, the level exit,
  hazards), looking up / down.
- Ghidra: `FixDisassembly.java` repairs the switch tables Ghidra resolved into the wrong
  segment and the garbage code they caused. It has been applied to the GUI project (ApplyTdInfo,
  FixDisassembly, then both again): 17 switches fixed, 13,137 of 13,138 source lines are code,
  and SETUPOBJ, SETUPHANDLES, GRABSTUFF, OPTIONMENU... decompile properly.

## Next steps

1. **Grabbing, damage and attacks** (next): GRABSTUFF (gems, secrets, level exit, hazards,
   warps; the tile types in notes "Grabbing"), HITPLAYER / SPIKECHECK / YOUHIT / YOUDIE /
   YOUCRUSH (and the inventory's hit points, blinking), the attacks (FYOUBEGINWHIP / WHIP /
   WHIPPINGFRAME and DOWHIP for the male, FYOUBEGINSHOOT / SHOOT and DOFIREBALL for the
   female) and ENEMYHIT, so enemies can be fought; then PROCESSLEVEL's looking and the status
   display. The decompiles are easiest to read from GhidraMCP's HTTP backend:
   `http://127.0.0.1:8080/decompile?name=FYOUWHIP`.
2. **More behaviours**, a module at a time; a `--sweep` run lists the animators a level
   reaches that aren't ported. By how many levels need them: DOGARGOYLE (10), DOCAVEHOLE (5),
   GOBLINSTAND, DOBOAR, MUDMANWALK, DOCAVESPIDER, DOCAVEFALLSPIKES, then the rest.
   `tools/animators.py` maps object types to animators.
3. **The game loop**: `_main` (RXMAIN.C) → title, menu, `NEWLEVELSETUP` → per-frame
   `PROCESSLEVEL`. Objects are `OBJstruct` (48 bytes) whose `Animator` field is a near pointer
   to a behaviour function (`DOGARGOYLE`, `YOURUN`...). Every behaviour decompiles readably
   now.
4. **Background flags**: bits 9-14 of background cells (probably collision/animation; see
   `RUNANIMTILES`, `ANIMTILE.C`) and the 0x600 bytes at the end of each `.led`. Bits 9-12
   are the collision flags and 13 (0x2000) marks grabbable tiles (notes, "Tiles").
5. Parallax backdrop (sky/horizon tiles show through gaps), palettes (`RXPALF.C`), sound
   (Apogee Sound System; VOC effects and MIDI music).
