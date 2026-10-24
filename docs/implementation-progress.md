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
build/roc.exe game/ROC.EXE                          # the game: title, menus, levels
build/roc.exe game/ROC.EXE --level 5                # a new game at level 5 (skips the title)
# headless: the title, Esc at tick 1000 (main menu), then Enter three times: a new game on Normal
build/roc.exe game/ROC.EXE --ticks 6000 --hold 0x01:1000-1020,0x1c:1500-1520,0x1c:2000-2020,0x1c:2500-2520 --screenshot out.png
# headless with files (ROC.CFG, roc.sco, saves) in a scratch folder: F2, Enter (slot 1), type "test", Enter
build/roc.exe game/ROC.EXE --level 0 --data-dir scratch --ticks 2500 --hold 0x3c:600-610,0x1c:800-810,0x14:900-910,0x12:950-960,0x1f:1000-1010,0x14:1050-1060,0x1c:1100-1110 --screenshot out.png
# headless: 1800 ticks (about 3 s) with the camera fixed and the player placed; save the view
build/roc.exe game/ROC.EXE --level 0 --pos 620,100 --player 770,250 --ticks 1800 --screenshot out.png
# headless with scripted keys: walk right, jump at ticks 1000-1100
build/roc.exe game/ROC.EXE --level 0 --ticks 3000 --hold right:0-3000,jump:1000-1100 --screenshot out.png
# headless test: move the player across the whole map; list every object at the end
build/roc.exe game/ROC.EXE --level 1 --ticks 40000 --sweep --list --screenshot out.png
# headless: the female with 10 gems throws fireballs; print the player's state every 80 ticks
build/roc.exe game/ROC.EXE --level 0 --female --gems 10 --player 470,231 --ticks 3000 --hold attack:900-920 --trace 80 --screenshot out.png
# headless: the goblin boss with an invincible player (--invincible sets _YouInvincible)
build/roc.exe game/ROC.EXE --level 16 --invincible --ticks 30000 --list --screenshot out.png
# headless: the Ross fight, the boss starting with 165 of its 170 hits taken (--boss-hits)
# headless: level 1 on difficulty 3 (_DiffMode; the default is 2), where the view scrolls by itself
build/roc.exe game/ROC.EXE --level 1 --difficulty 3 --invincible --ticks 6000 --list --screenshot out.png
# headless with the scripted joystick: the stick right, button0 (A: jump, by ROC.CFG's MyBUTTON0;
# button1-3 are X, B, Y, and start is Start)
build/roc.exe game/ROC.EXE --level 0 --joystick --ticks 3000 --hold joyright:0-3000,button0:1000-1100 --screenshot out.png
# headless: the fly-mode cheat (D + CapsLock + X held as PC scancodes), flying up and right
build/roc.exe game/ROC.EXE --level 0 --ticks 3000 --hold 0x20:500-520,0x3a:500-520,0x2d:500-520,up:600-900,right:600-2000 --screenshot out.png
# headless: the pause window (Tab, PC scancode 0x0f, held at tick 600), with 25000 points (two bonuses)
build/roc.exe game/ROC.EXE --level 2 --score 25000 --ticks 1200 --hold 0x0f:600-610 --screenshot out.png
# headless: collect prize 0x21 (CollectID index: the five shields) at the start, then change hero (Space)
build/roc.exe game/ROC.EXE --level 0 --gems 50 --collect 0x21 --ticks 4000 --hold item:1000-1010 --list --screenshot out.png
# headless: Ross beaten (attacks every 250 ticks), then the results, a name for the Hall of Heroes
# (Shift + H, E, R, O, Enter), the story (Esc) and "READY FOR MORE?" (Y): episode 2 starts
H=right:0-1700; for t in $(seq 3000 250 9000); do H=$H,attack:$t-$((t+10)); done
build/roc.exe game/ROC.EXE --level 7 --female --invincible --gems 60 --boss-hits 165 --data-dir scratch --ticks 14000 --hold $H,0x2a:9450-9520,0x23:9500-9510,0x12:9600-9610,0x13:9700-9710,0x18:9800-9810,0x1c:9900-9910,0x01:11000-11010,0x15:12000-12010 --screenshot out.png
```

`ROC.DAT` must be next to the EXE. The game runs at its 582 Hz tick rate. Keys (ROC.CFG's, by
default): arrows, Ctrl jump, Alt attack (the whip, or the female's fireballs), Space the special
item (change hero), Enter + up / down (or PgUp / PgDn) look; Esc the main menu, F1 help, F2 / F3
save / load, F8 / F9 quick save / load, F5 status display on / off, F6 / Tab / Pause the status
window, F4 the sound volumes, F10 quit, F11 gamma; a joystick or game controller when ROC.CFG turns it on (SETUP's choice) or
with `--joystick` (the stick or D-pad moves; in a level A / X / B / Y press the keys ROC.CFG's
MyBUTTON0-3 name: jump, attack, the special item and look as shipped; in the menus A is Enter and
B goes back; at a "Press Y" prompt X or Y says yes; Start is Esc, the main menu); the cheats: Backspace held,
then PRIME, MAGMA, ASTRAL, FIRE, RAIN, WIND or ETHER (the warp menu), and D + CapsLock + X the
fly mode (Ctrl lands). F12 saves `shotNNN.png` (the port's). In the menus: up / down or a line's
first letter, Enter (or attack) picks, Esc (or jump) goes back. ROC.CFG, roc.sco (the Hall of
Heroes) and the saved games (saving writes the port's `ROCP.SGn` and the original's `ROC.SGn`;
loading takes whichever of a slot's two is newer) live in the game's folder, or in
`--data-dir`; headless runs touch no files without `--data-dir`. `--level N` starts a new game
there without the hero's lightning entrance (the title's "Play a New Game" has it), as the
headless tests always started. In headless runs the fades are instant, the run stops wherever it
is when `--ticks` are used up (in a menu, on the title...), and the screenshot is the screen as
last shown. Objects whose behaviour isn't ported stay still, and each such animator is reported
once on stderr. Headless runs print the level, the player's position and animator, the
inventory and the level's counters (found / in the level) at the end.

Sound: effects and music through SDL2 audio on every platform; the music is the original's AdLib
driver on an emulated OPL2, so it sounds as it did on an AdLib or Sound Blaster. `--no-sound`
and `--no-music` turn them off; ROC.CFG's sound / music card -1 (SETUP's "none") does too. Headless runs are silent but
still time every sound, so the game behaves as it does with sound.

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
- Port, the castle creatures, the wisp and the chains (notes, "The castle, the wisp and the
  chains"): `src/rxcastle.c` DOKNIFEGUARD, DOGUARDKNIFE, DOBOMBGUARD, DOBOMB (also called in
  the air by GENERICUP / GENERICFALL for priority 0x3a) and DOBACKSTARS; new `src/rxdead.c`
  DOWISP with the `_Circle1` table; in `src/rxmush.c` DOCHAIN and DOCHAINEND; in `src/rxyou.c`
  DOAMBIENTNOISE (it only picks the sample while sound is a stub). SETUPOBJ gained type 0x31
  (the bomb); objects gained a `parent` field (a wisp light's wisp). Verified headless: on
  castle2 the knife throwers face the player, back off to the roof edges, raise an arm and throw
  every 8 x 0x79 ticks, and the knives fly at 145 px/s and hurt; on castle1 a bomb thrower lobs
  a bomb at tick 0x4b1 that lands and bursts into burning puffs, and three cracks of the whip
  kill it for a prize; castle2's stars show only on the night sky's colour-1 pixels; on dead2
  the wisp circles with its four lights and sparks and makes the player walk to it (about 109
  px/s); on goblin2 the player grabs the chain with up, climbs it, rides it on the boss's
  platform, jumps off, and drops off its bottom. A sweep of all 29 levels (both characters,
  attacking, 20,000 ticks) reaches no unported animator outside the bosses.
- Port, the first boss, Ross (castleb, level 7; notes "Ross"): new `src/rxross.c`
  (RXROSSF.C) FROSSWAIT, CRASH, PIECE, JUMP, ATTACK, THROW, LUNGE, SWORD, SHAKE (the view
  shake the other bosses use too), KNIFE, DIE and CLEAR; in `src/rxgenerl.c` BOSSHITPOINTS (the
  bar at the top), the bosses' hit table (DS:00c6, `boss_max_hits`), Ross's death in ENEMYHIT,
  and ROSSPIECE / ROSSJUMP in GENERICUP / GENERICFALL. SETUPOBJ gained types 0x18 (sword, shaker),
  0x19 (knife), 0x1a (ceiling stones) and 0x34 (the bar). `--boss-hits N` presets a boss's
  damage for testing. Verified headless: once the view reaches the map's end Ross crashes through
  the ceiling for 0x9c4 ticks with stones bouncing and the view shaking, lands on the right,
  throws five knives, jumps across with his sword, lunges from the left and runs back, and keeps
  the player inside the room; fireballs take him down (the bar follows), he flickers through 200
  explosions and 1000 ticks later the level is won and level 8 starts.
- Port, the dragon (swampb, level 10; notes "The dragon"): in `src/rxswamp.c` (RXSWAMPF.C)
  FDRAGONCENTER (the camera), FDODRAGONBOB, SPIT, FIRE, BOOM, NECK, FDRAGONNECKDIE and
  FDRAGONHEADDIE with the `_DragonMove` delay lines; the dragon's death in ENEMYHIT and
  DRAGONHEADDIE in GENERICUP / GENERICFALL; INITLEVEL's four necks; SETUPOBJ types 0x4b (neck)
  and 0x4c (fireball). `spawn_shake` (ROSSSHAKE) is now shared. Verified headless: the view pans
  to x 0xbe with the player frozen, the bar appears, the head bobs and every 200 ticks rears and
  spits a fireball that lands, bursts (shaking the view) and brings frogs out of the swamp; the
  necks ripple behind the head; struck down (a hit forced for the test: the scripted player is
  always knocked back before reaching the head) the head flies off and bounces, the stump
  thrashes and bleeds, and 2000 ticks later level 11 starts.
- Port, the crusher (mushromb, level 15; notes "The crusher"): in `src/rxmush.c` CRUSHERCENTER,
  MOVESTUFF and MAINTAINSTUFF. Verified headless: the room swings between view x 0 and 0x80
  (about 2 s each way, a pause and a thud at each end, the view shaking), the walls, platforms
  and chains stay pinned to the view, the walls shove the player along, and a player shoved
  into rock is crushed (FYOUCRUSH) and the level restarts.
- Port, the goblin boss (goblinb, level 16; notes "The goblin boss"): new `src/rxboss.c`
  (RXBOSSF.C) FBOSSCENTER, FBOSSHEADSPIT, SHAKE, FLOAT, BOUNCE, SPAWN, RETURN, DIE, FDOSPITTLE,
  FDOSPITTLE2 and FBOSSSHAKE; in `src/rxplat.c` SPINNINGPLATFORM, NOSPINPLAT and SPINPLATDIE; the
  boss's death in ENEMYHIT and BOSSHEADBOUNCE / DOSPITTLE2 in GENERICUP / GENERICFALL; SETUPOBJ
  types 0x4e, 0x4f and 0x57. `--invincible` sets `_YouInvincible` for testing (crushing still
  kills, as in the original). Verified headless: the camera waits 0x44c ticks after the player
  lands and scrolls to the head in the right wall, which spits gobs that splash; on the seventh
  round it shakes loose and bounces left with rocks falling, settles at x 0x40, raises three
  spinning platforms the player can ride (the lowest pushes a standing player into the wall and
  crushes them), floats, and once the platforms rest sinks into the left wall at x 0x54, y 0x80,
  whose rock body is rebuilt from tiles in line with the map; it then spits from there. A whip
  hit while it bounces (damage preset to 119 of 120) kills it: it flickers through 0xb4 bursts
  and level 17 starts.
- Port, the skeleton boss (deadb, level 20; notes "The skeleton boss"): in `src/rxskel.c`
  (RXSKELF.C) FDOSKELHEAD, FSKELWAIT, RISE, SINK, WALK, HACK, LUNGE, WAIT2, DIE and SKELHIT;
  INITLEVEL's two type 0x53 objects (`_Boss` and the head, `_Neck[0]`); the skeleton in
  GENERICUP / GENERICFALL / GENERICBEGINFALL (priority 0x4a), its death in ENEMYHIT; SETUPOBJ
  types 0x53 and 0x54 (the rocks). Verified headless: once the view reaches x 0xcf the bar
  shows and the skeleton leaps out of the ground at x 0xef in a spray of rocks, walks after the
  player, hacks (a hit point and a knock-back), sinks, rises at x 0x1d3 300 ticks later and
  lunges left to x 0x122, and so on; the whip or a fireball knocks the head off, and it flies,
  bounces about the room and blinks for 500 ticks; a strike on the skeleton (damage preset to
  19 of 20) kills it: it floats up flickering through explosions and bones, and 71 x 45 ticks
  later level 21 starts. A sweep of all 29 levels reaches no unported animator but the demon's.
- Port, the demon and the ending (demon, level 25; notes "The demon and the ending"): new
  `src/rxdemon.c` (RXDEMONF.C) FDEMONCENTER, WAIT, SPIT, SUMMON, SPOUT, DROP, DIE and DEMONFACE;
  in `src/rxplat.c` ROLLINGBOULDER and STILLBOULDER (SETUPOBJ type 0x50); in `src/rxgems.c`
  POWERUPSPARKS; the demon's death in ENEMYHIT. `game_start` loads level 25's PALCH table
  (`rocdemfd.txt`) and keeps `_VirtPal` / `_LevelPal`; the behaviours change the level's
  palette (SETPALETTE / GETPALETTE). NEWLEVEL's fixed starting views for levels 15, 20, 25
  and 27+ are in `game_view_on_player`. Verified headless: once the player lands and 500 ticks
  later, the demon comes out of the dark in 16 palette steps, the bar shows, and it cycles:
  eight fireballs, two fire boars from the view's sides, two sweeps of spikes (landing as
  flame vents) and potions, a boulder that bounces and rolls across (and crushes a player
  standing under it); a female's fireball from below (damage preset to 109 of 110) kills it.
  It shakes in a storm of explosions and fire, the screen fades to white, the room comes back
  without it (`demgone.bg`) with both heroes, sparks gather into a light that sinks to them,
  and the level ends (`_quitflag` 3; the game loop then goes on to level 26). A sweep of all 29
  levels reaches no unported animator.
- Port, INITLEVEL's level objects (notes "Scrollers"): in `src/rxplat.c` AUTOSCROLLER (type
  0x29: levels 1, 3 and 22 above difficulty 2), CAVESCROLLER (type 0x2b, level 13) and
  BOULDERSPAWNER (type 0x11, levels 17 and 19); in `src/rxcave.c` DOTORCH (type 0x2c, level 13's
  torches); INITLEVEL now also makes level 11 / 12's 16 night stars. `--difficulty N` sets
  `_DiffMode`. Verified headless: on level 1 at difficulty 3 the view scrolls right at 36 px/s
  and pushes a standing player along until a wall crushes them; on level 13 the view rises to
  the top, bounces, pauses, sinks slowly to y 100 and rises again, the player and the torches
  riding its bottom edge; level 17's four boulders drop as the view passes and roll to rest
  (one at x 0x8e0 with a crash); level 19 scrolls left by itself and drops its boulders.
  Sweeps of all 29 levels (and of levels 1, 3, 22 at difficulty 4) reach no unported animator.
- Port, the special item and the magic gems (notes "The special item and the magic gems"): in
  `src/rxyou.c` FDOTRANSFORMER (type 0x37) and the special item key in FYOUHORIZ, FYOUCROUCH and
  FYOUCLIMB; in `src/rxgenerl.c` DOLIGHTNING (type 0x0c); in `src/rxgems.c` SHIELDGEM (type 0x55),
  LIGHTNINGGEM (0x56), DEATHGEM (7), SWORDGEM (0x59) and DOFLYINGSHIT's magic items; INITLEVEL's
  lightning for a level entered by a warp; `_quitflag` 8 (a lightning warp to `_nextlevel`).
  `circle1` (`_Circle1`) is shared with the wisp. `--collect K` hands the player prize K.
  Verified headless: Space brings lightning down on the player and 100 ticks later the other
  hero stands there (the hit points and attack switching with them); after 800 ticks it works
  again; five shields circle the player and kill two bandits, two hits each; the lightning gem
  inverts the palette and keeps the player unhurt for 0xddd ticks; the death gem flashes white
  and kills what's in view; the weapon gem raises both weapons a step and holds the glowing
  player while sparks fly in. A sweep of all 29 levels reaches no unported animator.
- Port, the cat skeleton's death (ENEMYHIT, priority 0x4e): it falls apart into its eleven
  bones (type 0x38 debris, BonesID[0..10], thrown from its upper half), read from the raw
  disassembly since the decompile garbles the FRANDOM ranges. Verified headless on level 3: the
  male's whip (12 points) breaks it up into bones and sparks, and the bones are gone a second
  later, leaving its prize.
- Port, GRABSTUFF's last spawners (notes "Bolo cats and the snake platform"): in `src/rxgrab.c`
  FINDBOLOPOS and the tile type 0x68 spawner; in `src/rxforest.c` (RXFRSTF.C) FBOLOCATFALL,
  COCK, COCK2, THROW and FDOBOLO (types 0x27 / 0x28); in `src/rxplat.c` DOSNAKEPLAT (type 0x44,
  level 24 "goblins", not goblin2 as noted before). A map scan found the cat tiles on levels 1, 2
  and 6 and the snake's two starts on level 24 (rows 37 and 93). Verified headless: on level 2 a
  cat drops in beside the player, whirls a bolo, throws it rising at the player above and leaps
  out of the view's top to come down elsewhere; on level 6 the cats throw shuriken; on level 24
  the snake of bricks crawls along its track from the player's cell, its head running ahead and
  its tail following.
- Port, the last level details (notes "Reflections, looking and the fly mode"): the swamp's
  reflections (PARENTLISTADD / PARENTLISTKILL in `src/engine.c`, called from SPAWNOBJ, INITOBJ and
  OBJKILL; DOREFLECTION and `_ReflecTable` in `src/rxswamp.c`; SETUPOBJ type 0x5a), PROCESSLEVEL's
  looking up and down and the fly-mode cheat (`src/main.c`), XLEVEL (`src/rxgenerl.c`). The
  handoff called the reflections "the rafts' parent lists": they're the water's reflections.
  `--hold` takes raw PC scancodes (`0x20:FROM-TO`). Verified headless: on level 8 the player and
  25 statues, prizes and frogs have upside-down reflections in the water; on level 14 Enter + up
  raises the view from y 59 to 7 and Enter + down lowers it to 121, each coming back when let go;
  D + CapsLock + X flies the player up and right through the forest and Ctrl drops them. Sweeps of
  all 29 levels (also looking up and down) reach no unported animator.
- Port, PROCESSLEVEL's in-level windows (notes "Windows, cheats and hints"): new `src/ui.c`
  with the window layer (WINUP, WINCLEARTEXTAREA, PUTWINPIC, SYSPIXELWRITE, FONTSTRLEN, MENUSTR,
  the RocFontID font and its colour tables, ANIMATECURSOR, WINDOWCURSOR2, WINGETSTR) and the
  windows: STATUSWIN (F6 / Tab / Pause), PROMPTWIN (F10 quit), CHEATER and WARPMENU (the cheat
  codes), and the page reader (F1: PAGEREADER, READERWIN, READERFETCHER on rocinst.txt, shown on
  the reader's level 28 and its palette as the original does). In `src/main.c` the rest of
  PROCESSLEVEL: the level's clock, `_MenuComing`, F5 (status display), F11 (gamma, applied when
  the palette is shown, like SETPALETTE), the cheat codes, the extra hit points every 10000
  points, SPAWNHINTS and `_quitflag` 1 / 7. DOHINTS and SPAWNHINTS (the hint pictures) in
  `src/rxgenerl.c`. `game_start` now carries `_GameFlags` (the hero and the hints),
  `_OldGameFlags`, the settings and the next score bonus over from the level before; until now
  a hero changed with Space reverted at the next level. `--score N` starts with N points. The
  viewer's P pause is gone (the game pauses itself). Verified headless: the status window shows
  both heroes, the level line (E1L3: Threnody Wood) and the gems and score; Y in the quit prompt
  quits and another key goes back; PRIME, WIND and ETHER work, and typing 26 in the warp menu
  starts level 25; F1 opens page 12 of 33 and PgDn turns it; on level 0 the hints "Press ESC for
  the main menu" and "Press SPACEBAR to change heroes!" slide up in turn; F5 and F11 work; 25000
  points make two bonuses; the female carries into the next level. All sweeps are clean.
- Port, the game loop (notes "The game loop"): new `src/host.c` (the screen, palette, timer and
  keyboard on SDL2, or scripted headless: code draws into `Vga` and loops on `host_frame`, as the
  original loops on WAITTICS; MYFADEOUT / FADETOPAL), new `src/game.c` (`_MachControl`, NEWLEVEL,
  NEWLEVELSETUP with the loading screen, the easy maps and the level's counts, RUNLEVEL with the
  transitions, PROCESSLEVEL with every `_quitflag` case, PROCESSTITLEPAGE with TITLETIMER and
  the credits / Hall of Heroes cycle, PROCESSTEXT, GAMEINIT), new `src/save.c` (ROC.CFG and
  roc.sco in the original's formats; saved games in the port's own format, all objects and
  state), and `src/ui.c` rewritten as the original's blocking windows: MAINMENU, DIFFICULTYMENU,
  OPTIONMENU (hints, status display, blood and the blood password, CHECKPASSWORD), OPENMENU /
  SELECTMENU / CLOSEMENU, SAVEMENU, LOADMENU, QUICKMENU, DEATHRESTOREWIN, PROMPTWIN (all five
  questions), STATUSWIN, PERCENTWIN, FINALPERCENTWIN, HS_CHECKSCORES / HS_GETNAME, DISPLAYCREDITS,
  DISPLAYSCORES / SCOREPAGE, ENDWIN, PAGEREADER / READERWIN / READERFETCHER (stories don't wrap,
  the help does), WINGETSTR with typed characters. `Sess` (engine.h) holds what outlives a level.
  Fixed on the way: LEVELTIMER runs every 0x30 ticks (rate 0x0c in fours of ticks), not 0x24, so
  timings in the older "Verified" notes are a little off; Normal and the easier modes now load the
  easy maps (frsteasy.led...). The viewer keys ([, ], C, O) are gone. Verified headless: the title
  shows, after 15 s the credits, the Hall of Heroes' three pages and the title again; Esc opens
  the main menu, and a new game goes through the episode and difficulty menus to level 0 with the
  lightning entrance; a save (F2, a typed description) and a load (F3, "restore?") bring back
  every object as saved; F1 saves, reads the help on level 28 (page 12, PgDn) and restores the
  level; Esc, "Exit to Title", Y goes back to the title and F10, Y quits; a death offers the
  QuickRestore ("TRY AGAIN!") and Y restores, another key restarts the level; Ross beaten shows
  "Level Cleared!", the final results, the Hall of Heroes' name entry (roc.sco written), the
  episode's story and "READY FOR MORE?", and Y starts level 8. Sweeps of all 27 levels with both
  heroes are clean.
- Port, sound (notes "Sound"): new `src/audio.c` in place of the Apogee Sound System: an SDL2
  mixer for the effects (FX_PlayVOC, FX_SoundActive, FX_StopSound, FX_SetVolume; VOC files'
  8-bit PCM at about 11 kHz, linear interpolation, voices stolen oldest first when all are
  busy), silent and advanced by the game's ticks when there's no device (headless runs), and a
  MIDI file sequencer on a thread for the music (MUSIC_PlaySong looped, SetVolume through each
  channel's CC 7, FadeVolume, Pause / Continue) on winmm; new `src/sound.c` (RXSAMPLF.C):
  rocsound.txt's 183 samples with their chains, PLAYSAMPLE, MAINTAINSOUND, SOUNDLINKACTIVE /
  KILL, and NEWLEVELSETUP's song from rocsong.txt (the old one fading out over 0xe8 ms). In
  `src/ui.c` SOUNDMENU / VOLUMESELECT (F4, or the options' "Sound Volume"), the menus' click
  (sample 3) and pick (0xa9) sounds, which CLOSEMENU lets finish, MAINTAINSOUND in ANIMATECURSOR,
  the music paused in the pause window; the title's sound (0xb0 or 0xb1) and its music fading
  over a second on quitting; DOAMBIENTNOISE calls MAINTAINSOUND and needs more than 3 voices.
  ROC.CFG keeps the cards, voices and volumes. Verified: on forest1 a bat's bats1 -> bats2 ->
  bats3 -> bats1 loop plays under one handle (1) while birds, landings and hits get their own;
  a scratch test plays bats1.voc for its 0.89 s on SDL's dummy driver and runs a song (at volume
  0) through play, fade, pause and stop; all 18 songs parse; the sound menu's sliders move in
  steps of 16; the game-loop scripts and sweeps of all 27 levels (both heroes) are clean. Not
  heard by a person yet.
- Port, the AdLib music (notes "The AdLib music"): new `src/opl.c`, an OPL2 (YM3812) emulator
  written for the port (log-sine and exponent tables, the envelope rates, key scaling, the four
  waveforms, feedback, tremolo and vibrato; no rhythm mode, which the driver doesn't use), new
  `src/almidi.c`, Apogee's AL_MIDI driver and MIDI.C's volume layer read from the machine code
  (voice allocation, AL_SetVoiceTimbre / Volume / Pitch, the drum map, pitch bend, controllers
  7, 10, 94, 121, 123), with `_ADLIB_TimbreBank` (256 instruments), the drum map and the pitch
  tables read from ROC.EXE; `src/audio.c` runs the song's events inside the SDL audio callback,
  sample-accurately, and resamples the chip's 49716 Hz to 44.1 kHz. winmm is gone: the port
  needs nothing but SDL2 on Windows, Linux or macOS (CMake links libm off Windows). Verified
  with a scratch renderer through the same code: a flute's A4 sounds at 437.5 Hz (the driver's
  table gives 437.7: Apogee tuned a little flat), the songs render without clipping at about a
  third to half of the effects' loudness (OPL_GAIN 3, a judgment), and the game runs with music
  on SDL's dummy audio and video drivers; headless runs are unchanged. Not built on Linux or
  macOS yet, and not heard by a person yet.
- Port, the animated tiles and the background flags (notes "Animated tiles" and "Tiles"): new
  `src/animtile.c` with ANIMTILE.C's `_AnimateBack`, `_AnimateFore`, `_AnimateMask`,
  RUNANIMTILES and DEANIMATE, the timer task's counting (a step of each counter every 4 ticks),
  INITANTIL's `_AnTil` frames and SETUPHANDLES' per-level animations (water, waterfalls, torches,
  gems' glints, lava, secret1's foreground cycles). Read from the raw assembly (its near jumps are
  mis-segmented in Ghidra) and the two CS jump tables. The flags: bit 14 of the background cells
  is never tested by ROC.EXE (only some forest cells have it), bit 15 is never set, and the 0x600
  bytes at the end of each `.led` are the level editor's per-tile default flags, which LOADLEVEL
  never reads; so the port's collision and grab bits were already complete. Verified headless:
  every level's animations find their cells (the caves' 19 + 4 background cycles, goblin2's 9
  torch cells, the castle's gems...); on goblin2 the torches' flames and the gems' glints change
  between ticks 100 and 180; save and load on goblin2 work; a sweep of all 27 levels with both
  heroes is clean. Not compared frame by frame with DOSBox.
- Port, the joystick (notes "The joystick"; the "parallax backdrop" this list had next doesn't
  exist: the cell drawer draws opaque tiles, notes "Graphics"): in `src/host.c` SDL's first
  joystick (a game controller: left stick or D-pad and A, B, X, Y; or a plain one), MYJOYREADER
  writing the stick and buttons into the keys by `_KeyTrans` and `_MyBUTTON0-3`, with the
  threshold from ROC.CFG's calibration; ROC.CFG's JoystickOn, MyBUTTON0-3 and calibration read
  and written (`Sess`); `--joystick` and scripted joystick holds for headless runs; in `src/ui.c`
  "PRESS A BUTTON!" in the results windows and the reader / Hall of Heroes taking button 0's and
  1's keys (they were hard-wired to jump and attack). Headless holds now press and release keys at
  their ends instead of being re-applied each frame. Verified headless: the ten documented test
  runs give the same reports and screenshots as before; walking right and jumping by the
  scripted joystick is identical to the keyboard; with ROC.CFG's joystick off the stick does
  nothing; centring the stick lets go of a held keyboard arrow as in the original; the title's
  menus start a game by buttons alone; button 0 leaves the reader; the results window says "PRESS
  A BUTTON!"; sweeps of all 27 levels are clean. After a play test with a real controller: X
  attacks and B changes hero (A, X, B, Y are gameport buttons 0-3), Start is Esc (the main menu in
  a level), and outside the level's own frames A is Enter and B Esc, with X and Y as Y at the
  "Press Y" prompts (`host_pad_mode`; scripted hold `start`). Verified headless: X fires as Alt
  does, B changes hero as Space does, A jumps as Ctrl does (identical runs); Start opens the main
  menu, B leaves it, A picks a new game; at F10's prompt X and Y quit and A goes back; on the title
  A alone starts a game; the documented keyboard runs are unchanged.
- Port, the original's saved games (notes "The original's saved games"): `src/save.c` loads
  `ROC.SG0-7` (RESTOREGAME / MAKEOBJECT): the header, the maps, the globals and the objects, with
  the animators numbered by the DS:059a table (read from ROC.EXE), images by `_Img` index and
  `_You` / `_Boss` / `_HitPts` / `_Neck` by the record's global byte; each slot is the newer of
  `ROCP.SGn` and `ROC.SGn` (saving still writes `ROCP.SGn`). The saved maps number the tiles as
  TILEBUFFOPTI compacted them, so `animtile_opti_numbers` (`src/animtile.c`) replays it and the
  cells are translated back. Verified headless with the game folder's ROC.SG0-2 in a scratch
  `--data-dir`: the load menu lists "Lev1", "Lev2", "ep2.1"; each loads (castle1, Ross's room, the
  dragon) with the saved view, hit points, gems, score and found counts, the maps right (checked
  cell by cell against the levels: only cells the player changed differ) and plays on for 20000
  ticks; whichever of ROC.SG0 / ROCP.SG0 is newer is the one loaded; the documented runs and the
  port's own save / load are unchanged. The original's ROC.SGT (a reader save) isn't read.
- Port, writing the original's saved games (notes "The original's saved games"): saving a
  slot writes `ROC.SGn` in the original's format (SAVEGAME / STOREOBJECTINFO in `src/save.c`:
  the animators by the DS:059a table, images by `_Img` index or the first empty slot, the
  globals, the tiles renumbered as TILEBUFFOPTI had them, worked out once when the level starts)
  and then the port's `ROCP.SGn`, which the port goes on loading. Reading fixed on the way: an
  object without an image (a camera) stays without one instead of loading a sprite set.
  Verified headless: castle1 from the DOS game's save, saved again by the port, gives a file of
  the same size with the same BGMAP bytes and the same 55 objects; on all 27 levels a save
  loaded from either file gives the same report and object list, but where the original's
  format lacks the seed, `_DragonMove` or the equal-priority order (levels 7-10 and 26, as the
  DOS game would); the parser in the session scratchpad reads every port-written file to its
  end; the documented runs are unchanged. Not loaded in the DOS game yet.
- Ghidra: `FixDisassembly.java` repairs the switch tables Ghidra resolved into the wrong
  segment and the garbage code they caused. It has been applied to the GUI project (ApplyTdInfo,
  FixDisassembly, then both again): 17 switches fixed, 13,137 of 13,138 source lines are code,
  and SETUPOBJ, SETUPHANDLES, GRABSTUFF, OPTIONMENU... decompile properly.

## Next steps

1. Load a port-written ROC.SGn in the DOS game (DOSBox) to confirm it; the OPL3 stereo paths of
   AL_MIDI (`_AL_Stereo`, port 222h: only for a Sound Blaster Pro / 16 set up so); a build on
   Linux and macOS; the interactive window, the sound, the animated tiles and the joystick (with
   a real controller) haven't been tried by a person yet.
