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
  with `--target` into a scratch folder and disassemble the EXE image directly. In the raw
  image, far-call segments are unrelocated (`lcall 0x2c7c:...` is Ghidra's 3c7c).

## Working tips

- Bulk decompiles: GhidraMCP's HTTP backend answers `GET /decompile?name=FUNCNAME` with JSON
  (`data.code`). A small script saving a list of functions to files beats reading each one
  through the MCP tool.
- Animator constants in decompiles: `DAT_4a77_xxxx` is the value xxxx; `DAT_5000_xxxx` is
  xxxx + 0x5890; string labels like `s_..._4a77_0fe3 + 5` are 0x0fe8. When in doubt, read the
  immediate from the machine code (`C7 44 2C lo hi` = `MOV [SI+2Ch], imm`).
  `tools/animators.py` lists each object type's animators this way.
- Arguments the decompiler drops (`PLAYERUP()`) or hides in inlined `rand()` maths: read the
  pushes before the call in the raw disassembly.
- Editing: the Bash tool collapses `\\` in heredocs, so `\n` inside a Python edit script
  passed by heredoc breaks C strings. Write such scripts to a file first (or use the Edit
  tool). Keep files LF: Python on Windows writes CRLF unless opened with `newline=""`.
- Testing: headless runs with `--hold`, `--player`, `--list`, `--trace` and `--sweep` (see
  Running); compare traced positions and speeds with the decompiled constants (ticks are
  582/s). `--female --gems N` tests the fireballs.
- Disassembly: GhidraMCP's `GET /disassemble_function?address=SEG:OFF&offset=N&limit=1000`
  pages through a function (check `has_more`). Use it for switch tables (`JMP CS:[BX+t]`; read
  the table from the EXE), for `OBJIMAGE()` calls the decompiler shows without arguments, and
  for the `rand()` maths (`PUSH 0; PUSH 8000h; CALL rand; ... MOV AX,n; CALL LXMUL` is
  `FRANDOM(n)`).
- Animator constants written as `_Img.XxxID + n` are DGROUP offsets too: `_Img` is at
  DS:3a44 and the field offsets are in the tdinfo FIELD rows (`_Img.ChainID` = 0x3e54,
  GENERICBEGINJUMP). OBJBLINK's Ghidra parameters are in reverse: the real call is
  OBJBLINK(objp, blinks, on, off).

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
# headless: the female with 10 gems throws fireballs; print the player's state every 80 ticks
build/roc.exe game/ROC.EXE --level 0 --female --gems 10 --player 470,231 --ticks 3000 --hold attack:900-920 --trace 80 --screenshot out.png
```

`ROC.DAT` must be next to the EXE. The level runs at the game's 582 Hz tick rate. Game keys:
arrows, Ctrl jump, Alt attack (the whip, or the female's fireballs: `--female`), Enter + up /
down look. Viewer keys: [ and ]
change level, C free camera (the arrows move the view), O hides the objects, P pauses, F12
saves `shotNNN.png`, Esc quits. Reaching the exit goes on to the next level; dying restarts
the level 100 frames later (no menu or story screens yet). Objects whose behaviour isn't
ported stay still, and each such animator is reported once on stderr. Headless runs print the
player's position and animator, the inventory and the level's counters at the end.

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
- Port, grabbing, damage and attacks (notes, "Grabbing" and "Damage, attacks and the
  inventory"): `src/rxgrab.c` GRABSTUFF (warps, secrets, gems, the exit, stakes, water and
  pits, spikes, back-to-start); in `src/rxgenerl.c` HITPLAYER, ENEMYHIT, ENEMYHITPAUSE,
  DOEXPLOSION, EXPLODERSPAWNER, SPLASHSPAWNER, DOCOLLECTABLE, statues being smashed and
  DOPROJECTILE's landings (blood splats, knives, spikes); SPIKECHECK in `rxforest.c`;
  `src/rxgems.c` DOFLYINGSHIT (scoring prizes); in `src/rxyou.c` the whip (FYOUBEGINWHIP,
  FYOUWHIP, WHIPPINGFRAME, DOWHIP), fireballs (FYOUBEGINSHOOT, FYOUSHOOT, DOFIREBALL), YOUHIT,
  YOUBEGINIMPALE, YOUDIE, YOUCRUSH, YOUDROWN, DROPGEMSPAWNER, the status display (DOHITPOINTS,
  DOGEMNUMBER) and DOBLOOD; the attack keys in the player's states. `src/engine.c`: the
  inventory (`Inv`, kept from level to level), OBJCOLLIDE, OBJBLINK and blinking drawing,
  LEVELTIMER and OFFKILLER, the castle warp tables. `src/main.c`: PROCESSLEVEL's `_quitflag`
  3 / 4 / 6. Verified headless: a bandit's club and a bat each take a hit point, knock the
  player back, stun on landing and leave 11 timer runs of protection; the whip (3 segments)
  and fireballs (a gem each) kill a bandit, which drops a prize; gems are counted on the
  display; the forest1 exit leads to level 1 with the inventory; the stakes impale and the
  caves' water drowns, then the level restarts; a castle1 warp moves the player. Every level
  runs 20,000 ticks with both characters attacking during a sweep.
- Port, the next creatures (notes, "Enemies"): in `src/rxforest.c` (RXFRSTF.C) the gargoyles
  (FDOGARGOYLE, FDOGARGOYLEFIRE), the flame vents (FDOCAVEHOLE) and the spiders (FDOCAVESPIDER,
  with their thread written into the foreground map and their poison); in `src/rxcave.c`
  DOCAVEFALLSPIKES and DOTORCHPUFF; `src/rxskel.c` (RXSKELF.C) the boars (FDOBOAR,
  FDOBOARATTACK, FDOBOARSPEAR, and the fire boar's three fireballs); `src/rxgoblin.c`
  (RXGOBLIN.C) GOBLINSTAND, GOBLINRUN, GOBLINHIT, GOBLINFIST and GOBLINJUMP (hooked into
  GENERICUP / GENERICFALL with the spear); `src/rxswamp.c` (RXSWAMPF.C) FMUDMANWALK and
  MUDMANHIT. SETUPOBJ gained the spawned types 0x00 / 0x38 (mud), 0x2f (poison), 0x3f (fist)
  and 0x52 (spear). Verified headless: gargoyle fireballs leave its mouth at 142 px/s at ticks
  1 and 401; falling spikes rattle and drop as the player walks under; spiders let themselves
  down on a drawn thread, climb back and spit three drops; vents puff flames from tick 1500 to
  2000; a goblin turns three times, runs at 178 px/s, punches the player and runs on; a boar
  throws its spear (which hits and bounces back), then walks at 37 px/s turning every 0x8fd
  ticks; a mud man rises 8 pixels out of the ground, walks at 24 px/s and loses 4 points and 6
  blobs to each crack of the male's whip, dying at the third. All 29 levels run 20,000 ticks
  for both characters, attacking, during a sweep, with no unported object types.
- Port, the cave, swamp and mushroom creatures (notes, "Platforms that block" and "More
  creatures"): MAINTAINBLOCK in `src/rxplat.c`, now called by every solid object that uses it
  (falling bricks, square platforms, boulders, the goblin boss's platform, spiked walls in
  SPIKECHECK); in `src/rxplat.c` DOSQUAREPLAT, DOFALLINGPLAT (bricks and crushers), BRIKSHAKE,
  DOCONTAINERBOMB (statue bombs), SWAMPRAFT, FROGPLAT; `src/rxcastle.c` BOMBEXPLODER; in
  `src/rxcave.c` DODROP; in `src/rxforest.c` FDOCATSKELETON; in `src/rxswamp.c` FDOMOSQUITO,
  FDOFROG, FFROGLAUNCH, FROGJUMP, FROGLAND and GRABSTUFF's frog spawner (tile type 0x69), with
  GENERICBEGINFALL moving a leaping frog from the foreground list to the main list;
  `src/rxmush.c` DOSPOREHOLE, DOSPORE; in `src/rxgoblin.c` DOROCKMONSTER, ROCKMONSTERFALL,
  ROCKMONSTERHIT. SETUPOBJ gained types 0x3a (a split rock monster), 0x3e (frog), 0x48 (spore)
  and 0x5b (the raft's front). Verified headless: a square platform goes round its 64-pixel
  square every 4096 ticks; a brick shakes, falls on the player (who crouches, then is crushed)
  and sets into the floor; the cat skeleton walks the slopes and hurts; drips fall; the giant
  frog platforms surface and leap about 100 pixels; mosquitoes flit round the player; frogs
  leap out of the swamp and sit on land; the raft carries the player right at 36 px/s and stops
  at its landing (x 0x344); spore holes spit spores that hurt; a rock monster rolls at 31 px/s,
  sheds 6 pieces a crack of the whip and (tested with its damage preset to 38) splits into two
  medium ones; a statue bomb touched by the player bursts into 9 puffs and three flaming blobs.
  All 29 levels run 20,000 ticks for both characters, attacking, during a sweep.
- Ghidra: `FixDisassembly.java` repairs the switch tables Ghidra resolved into the wrong
  segment and the garbage code they caused. It has been applied to the GUI project (ApplyTdInfo,
  FixDisassembly, then both again): 17 switches fixed, 13,137 of 13,138 source lines are code,
  and SETUPOBJ, SETUPHANDLES, GRABSTUFF, OPTIONMENU... decompile properly.

## Next steps

1. **More behaviours** (next), a module at a time; a `--sweep` run lists the animators a level
   reaches that aren't ported (it needs `--screenshot`, or it opens a window). What a sweep of
   every level still reaches: DOAMBIENTNOISE (every level; sound only); the castle creatures
   in RXCASTLE.C, DOBOMBGUARD (5, 6; it throws DOBOMB) and DOKNIFEGUARD, DOBACKSTARS (6);
   DOWISP (18); DOCHAIN / DOCHAINEND (21, 23); and the bosses and their cameras: ROSSWAIT (7),
   DODRAGONBOB / DRAGONCENTER (10), CRUSHERCENTER (15), BOSSCENTER (16), DEMONWAIT /
   DEMONCENTER (25). The skeletons (SKEL*, DOSKELHEAD) in RXSKELF.C are spawned, not placed;
   DOTORCH (type 0x2c) isn't placed by INITOBJ either. `tools/animators.py` maps object types
   to animators. Left over from the damage milestone: the special item (key 6, spawn type 0x37
   / DOTRANSFORMER), the magic gems (DEATHGEM, LIGHTNINGGEM, SHIELDGEM; SWORDGEM's animation
   for a weapon upgrade, which is applied without it), GRABSTUFF's other spawners (bolo cats
   0x68 via FINDBOLOPOS, goblin2's snake platform), the cat skeleton's bones in ENEMYHIT, the
   rafts' parent lists (PARENTLISTADD), the bosses' deaths, PROCESSLEVEL's looking up / down, XLEVEL
   (the cheat fly mode). The decompiles are easiest to read from GhidraMCP's HTTP backend:
   `http://127.0.0.1:8080/decompile?name=DOTRANSFORMER`.
2. **The game loop**: `_main` (RXMAIN.C) → title, menu, `NEWLEVELSETUP` → per-frame
   `PROCESSLEVEL`. Objects are `OBJstruct` (48 bytes) whose `Animator` field is a near pointer
   to a behaviour function (`DOGARGOYLE`, `YOURUN`...). Every behaviour decompiles readably
   now.
3. **Background flags**: bits 9-14 of background cells (probably collision/animation; see
   `RUNANIMTILES`, `ANIMTILE.C`) and the 0x600 bytes at the end of each `.led`. Bits 9-12
   are the collision flags and 13 (0x2000) marks grabbable tiles (notes, "Tiles").
4. Parallax backdrop (sky/horizon tiles show through gaps), palettes (`RXPALF.C`), sound
   (Apogee Sound System; VOC effects and MIDI music).
