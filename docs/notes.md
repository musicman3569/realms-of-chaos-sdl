# Reverse engineering notes

Addresses are as Ghidra shows them for `game/ROC.EXE` (load base segment 1000).

## Executables

| EXE          | Packed?     | Notes                                                              |
|--------------|-------------|--------------------------------------------------------------------|
| ROC.EXE      | no          | the game; 297,184-byte image + 246,236 bytes of debug info         |
| ROCHELP.EXE  | LZEXE 0.91  | help viewer; `tools/unlzexe.py` unpacks it (drops 31 KB of appended data, probably the help text) |
| SETUP.EXE    | no          | setup program (Borland)                                            |
| ULTRAMID.EXE | no          | Gravis UltraSound MIDI driver, not part of the game                |

Unlike Pharaoh's Tomb and Monuments of Mars, ROC.EXE isn't packed, so Ghidra analyzes the
original file directly.

## Compiler

- **Borland C++ 3.x** (`Borland C++ - Copyright 1991 Borland Intl.`), real mode, MZ header
  with the TLINK signature `FB 50 6A 72`. Large model: one code segment per module
  (TLINK packs several into one 64 KB frame, so CODE_0 holds many modules), one DGROUP at
  `4a77`.
- Game code uses the **pascal** calling convention: uppercase public names (`DOGARGOYLE`),
  arguments pushed left to right, callee pops (`RET n`). The runtime library, the sound
  library and a few game functions are **cdecl** (`_main`, `_KBD_Init`). Ghidra's x86-16
  compiler spec has no pascal convention, so pascal functions get `__stdcall16near/far`
  (callee pops) with custom parameter storage in source order.

## Turbo Debugger symbols

ROC.EXE was linked with debug info (`TLINK /v`): the data after the load image is a Turbo
Debugger 3.x symbol table (magic `52FB`, version `0310`). It has the original names of
functions and globals, the source file and line of every statement, every function's
parameters and locals, and the C types (structs, unions, typedefs). The table layout is
documented at the top of `tools/tdinfo.py`.

Applying it to Ghidra is two steps:

```
python tools/tdinfo.py game/ROC.EXE analysis/ROC.tdinfo.txt
```

Then in Ghidra: Script Manager → Manage Script Directories → add `tools/ghidra_scripts`, then
run `ApplyTdInfo.java` and pick `analysis/ROC.tdinfo.txt`. It's safe to re-run. It:

- defines the structs, unions and typedefs under the `/TurboDebugger` category
  (`OBJstruct` is the 48-byte game object every behaviour function works on);
- creates the ~190 functions analysis missed (object behaviours reached only through the
  `Animator` function pointer) and names all of them;
- sets each debug-info function's real prototype and typed locals;
- labels and types the globals in DGROUP;
- maps every source line (Ghidra's source map, plus a `line N` EOL comment) and writes a plate
  comment with the source file, module, convention and variable list.

Ghidra quirk: for 7 of the 1,344 near `CALL`s (e.g. `STRCPY` → `STRLEN` at 3ffb:0054) Ghidra
doesn't wrap the 16-bit target offset, so the call target lands 64 KB away, in a different
segment. Two of them land in DGROUP, where analysis then made bogus functions (plus a
`caseD_0` from a misread jump table) on top of real globals. The script removes
analysis-made functions in DGROUP before typing the data. The 7 calls themselves still point
at the wrong addresses in Ghidra. Near jumps can wrap the same way; FixDisassembly follows them
to the right place but leaves their references as they are.

### Repairing the disassembly (`FixDisassembly.java`)

Ghidra's MZ loader gives each module's code a segment, but for switch tables it reads the
table and the case targets with the wrong segment base. Borland compiles a dense `switch` as
`CMP BX,n / JBE / JMP default / SHL BX,1 / JMP word ptr CS:[BX+table]`, and the table holds
offsets in the function's own segment. For SETUPOBJ (2a35:001f) Ghidra used `2000:xxxx`, so
the 96 "cases" landed in other modules. It disassembled garbage there, and the garbage
overlapped real instructions, so real code was cut off too. SETUPOBJ decompiled into calls to
`DOSWRITE`, and GRABSTUFF lost most of its body to one stray target from SETUPHANDLES. Before
the fix, 1,525 of the 12,919 source-line starts were not instruction starts. Some data items
analysis had typed inside code had the same effect (OPTIONMENU).

`tools/ghidra_scripts/FixDisassembly.java` repairs this. It takes the same
`analysis/ROC.tdinfo.txt`:

1. For the 17 switches of that shape it reads the table at `CS:table`, replaces the flow
   references and writes a jump-table override. Every target lands on a source-line start.
2. It walks the real code along fall-throughs and jumps, starting from what the debug info
   vouches for: the 13,138 line starts, the case targets, then the named functions and last
   the unnamed ones. Anything overlapping an instruction on that walk is cleared, including
   data in code segments. It never clears code the walk has already reached, or the globals
   in DGROUP (unless a line start says it's code). It also wraps near jump targets into the
   jump's own segment (see the quirk below).
3. It clears the garbage reachable only from the old switch targets, and recomputes every
   function body.

It also removes `line N` comments that aren't on a line start (an earlier ApplyTdInfo run had
put about 20 of them a byte early), and it keeps all other comments. Run it after
ApplyTdInfo; then run ApplyTdInfo and FixDisassembly once more. The second round picks up what
the first repair uncovered: functions the garbage had hidden, functions auto-analysis made in
DGROUP meanwhile (ApplyTdInfo removes them), and a 17th switch (`_MV_ErrorString`). After
that, a run changes nothing. In the GUI project, 1 of the 13,138 line starts is still not code
(headless tests left 28aa:00a5 in SAVEGAME and 2f81:0639 in WRITETEXT). The case labels in the decompiler are target addresses (`case 0x2a976:`), not
case values; the index is the position in the table.

The scripts can be tested without touching the open project: copy `ghidra/realms-of-chaos.gpr`
and `.rep` to a scratch folder and run `support/analyzeHeadless.bat <copy> realms-of-chaos
-process -noanalysis -postScript FixDisassembly.java analysis/ROC.tdinfo.txt` (with
`JAVA_HOME` set to Ghidra's JDK).

What the debug info doesn't cover: the ~90 runtime-library helpers without public names
(they stay `FUN_…`), and register variables (SI/DI); those are listed in the plate comment.

## Data files

`ROC.DAT` is a plain **LHA archive**: 466 files with level-1 headers, 464 compressed with
`-lh5-` and 2 stored (`-lh0-`), ending in a zero byte. The game reads it with Haruhiko Okumura's
ar002 decoder (`HUF.C`, `DECODE.C`, `MAKETBL.C`; `AREAD` reads `_origsize` bytes in 8 KB
chunks and checks the CRC-16). `tools/rocdat.py` lists it, or extracts every file into
`extracted/` and checks each CRC:

```
python tools/rocdat.py game/ROC.DAT extracted
```

| Ext    | Count | What it is (first look)                                                  |
|--------|-------|---------------------------------------------------------------------------|
| `.voc` | 185   | Creative Voice sound effects                                              |
| `.spr` | 140   | sprite sets (1,039 frames in all), see below                              |
| `.led` | 42    | level maps (29 levels + easy-mode variants such as `cas2easy.led`)        |
| `.bg`  | 22    | background tile sets (16×16 tiles)                                        |
| `.mfg` / `.mbg` | 14 each | masked foreground / "bonus" tiles drawn over the background   |
| `.mid` | 18    | standard MIDI music                                                       |
| `.txt` | 12    | story and help text with `{n}` colour codes                              |
| `.pal` | 8     | 768-byte VGA palettes (6-bit components)                                  |
| `.vga` | 5     | full screens: 768-byte palette, then 320×200 mode 13h pixels              |
| `.bin` | 3     | 80×25 text-mode screens (character + attribute), e.g. `rocreg.bin`        |
| other  | 3     | `roc1warp.008/.009` (warp coordinates?), `text.old`                       |

### Graphics

The game runs in VGA **Mode X** (unchained 320×200, four planes), and both the tiles and
the blitters are organised around planes. `tools/extract_gfx.py` renders everything to
`gfx/` (screens, sprite sheets, tile sheets, and full maps of all 29 levels):

```
python tools/extract_gfx.py extracted gfx
```

The format details are in that script's docstring. In short:

- **Sprites** (`LOADSPRITE`, `_DrawSprite` in DRWSPRIT.ASM): each frame is a `SPRITE`
  struct (size, hotspot, hit box) followed by a column-major stream of `(op, count)` pairs:
  0 skip rows, 1 copy pixels, 2 next column, 3 end. After it, at `Data+PostOff`, there's a
  table of column start offsets, used for clipping. Judging by their names, `_RevSprite` and
  `_FlipSprite` draw the same data mirrored, so the sprite files hold only one facing.
- **Background tiles** (`.bg`): 256 bytes each, stored as four 64-byte planes. Drawn by
  `_DrawSolidBlock`.
- **Foreground/bonus tiles** (`.mfg`/`.mbg`, `_DrawMaskBlock` in DRWBLOCK.ASM): masked
  tiles, plane by plane, as `(op, count)` pairs (0 skip, 1/2/3 write both/left/right byte,
  4 next plane, 5 end).
- **Levels** (`LOADLEVEL` in LOADLEV.C, drawn by `FUN_3cb4_0d58`): background cell =
  `value & 0x1ff` into bg layers 1-3 (240 tiles each) plus flag bits 9-14 (see "Tiles: types and
  collision");
  foreground byte = 0 or a 7-bit tile index, from `.mfg` if bit 7 is set and `.mbg` if not.
  The foreground map is an `FGMAP`: a u16 `Cols`, then the table (LOADLEVEL reads
  Rows*Cols + 3 bytes, so one spare byte follows). Reading the table from offset 3 instead
  shifts every foreground tile one cell left (tree tops off their trunks, sky showing through
  the gaps), which the port did at first.
- The cell drawer (3cb4:0d58) draws one opaque 16x16 background tile per cell, from layer 1, 2
  or 3 by its number (below 0xf0, below 0x1e0, the rest; each layer is 240 tiles in the `.bg`,
  as every level's counts have it), then masks the foreground or bonus tile over a copy of it.
  There is no backdrop layer and no parallax: RUNLEVEL draws the map and the three object lists
  and nothing else (an earlier handoff guessed at one).
- Level *n* uses `name[n].led` and the tile set `name[set[n]]`, where the names come from
  `roclevel.txt` and `set[]` is a 29-byte table at DS:033e (e.g. forest2 uses forest1's tiles).
- Each `.led` ends with 0x600 bytes that `LOADLEVEL` doesn't read (after its 0x400/0x100/0x100
  type tables): the level editor's default flags per tile, in the same layout (see "Tiles").

Source modules (from the line-number table): the game is `RX*.C` plus `rocxms.asm`,
`rocport.asm` and `rocfile.asm`; the engine is `SPRITE.C`, `MOVE.C`, `ANIMTILE.C`,
`LOADLEV.C`, `KBD3.C`, `TIMER.C`, `XMS.C`, `EMS.C` and the `DRW*.ASM` blitters;
`HUF.C`/`DECODE.C`/`MAKETBL.C` look like the decompressor (probably for `ROC.DAT`); the sound
library is `SOURCE\*.C` (`BLASTER.C`, `AL_MIDI.C`, `MULTIVOC.C`, `FX_MAN.C`, `TASK_MAN.C`…),
which looks like the Apogee Sound System.

## Level objects

**Placing them** (`LOADOBJ`, `INITOBJ` in RXOBJF.C). The `.led` object list is a linked list
of `SPRITE_LIST` (62 bytes: link, sprite pointer, `name[20]`, `info[32]`, `x`, `y`). LOADOBJ
matches each name against a table of `IMAGEDATA` (9-character name + frame count) and loads
`name.spr` into the matching slot of `_Img` (an `IMAGEstruct` of `SPRITE far*` arrays:
`_Img.BatID`, `_Img.MalWalkID`...), once per set. Entries with `x == 0xFFFF` only load a
set (fonts, the other player sprites, things spawned later). For the others:

```
x = list.x - frame0.ImgXoff
y = list.y - frame0.ImgYoff - 8        (16-bit unsigned)
if y <= Rows*16 - 1: INITOBJ(set, x, y, info)
```

So the list stores the top left corner of frame 0, 8 pixels low, and the object's x / y is its
hotspot, in map pixels (tile row r is at y = r*16, the same coordinates as `_OrgX/_OrgY`).

The `IMAGEDATA` names are the `IMAGEstruct` field names without `ID`, lower case: 141 sets
in all, `ArrowID[3]` to `WolfID[6]`, 1,059 frame pointers. The behaviours index these arrays
directly, sometimes past their end (DOBIRD's `BirdID[4]` is `BirdpoopID[2]`), so the port keeps
the same flat table: `tools/gen_imagetab.py` generates `src/imagetab.h` from the debug info.

INITOBJ allocates the `OBJstruct` (48 bytes) and a `USERstruct` (27 bytes), then picks the
behaviour by comparing the sprite set pointer with `_Img` slots: it puts an object type index
in `UserPtr->FrameNo` (bat 0x1f, catwalk 0x22, catband/catmace 0x23, statue 0x15, leaf 0x10,
gargoyle 0x1d, wolf/dog 0x20, player 4...) and calls SETUPOBJ, whose 96-case switch on that
index sets the `Animator`, speeds, frame counts and list priority. `SPAWNOBJ(type, list, ...)`
uses the same indices for objects made at run time. INITOBJ then resets `FrameNo` to 0 and sets
the first frame (FOBJIMAGE). It's frame 0, except for the player (`_YouWalkID[9]`, or
`_YouJumpID[1]` on levels 13, 16, 22 and 25) and level 4's gargoyles (`GargoyleID[1]`).
`malwalk` in the list marks the player's start; `_YouWalkID` points at the male or female set
by `_GameFlags` bit 2. `info` is a per-type parameter: the bat's `l` / `r` set `xDir`; the
statue's number is a hint or gem index; the catwalk's `vt07` / `vb09` (and `h`, `p`...)
encode a patrol (direction, distance in tiles).

**Drawing them** (the list drawer at 3cb4:064c, `OBJVISIBLE`, `OBJONSCREEN`):

```
xDir == 0:  draw at (x + ImgXoff, y + ImgYoff)                 _DrawSprite / _ClipSprite
xDir != 0:  draw at (x - (Cols - BoxCols + ImgXoff), y + ImgYoff), mirrored   _RevSprite
```

The sprites face left; `xDir` 1 mirrors them. The blinking variants (`_DrawColorSprite`...)
draw every pixel in `BlinkColor` while `Blinks` counts down.

**Depth.** Foreground (`.mfg`) tiles stand in front of the main list: once RUNLEVEL has drawn
the main list, it calls 3cb4:0bec for every visible main-list object, which masks the `.mfg`
tiles (foreground byte bit 7 set) of the cells the object's image covers back over it (only
cells inside the view's 22 x 14). That puts the forest's berry bushes and the swamp's
front trees over the player and the enemies. Bonus (`.mbg`) tiles stay behind, and the
foreground and slow lists are drawn afterwards, on top of everything.

## Running objects (the engine)

**The loop** (`RUNLEVEL`, 3cb4:157c). Each frame: `RUNTIMERS`, `RUNANIMTILES`,
`ANIMATORUPDATE(_Update)`, draw the main, foreground and slow lists, flip pages, call the
level's `FrameHook`, `SCREENLISTUPDATE`, and set `_Update` to the timer ticks the frame took.

**The clock.** `_MachInit` calls `_speedup(0x20)`, which schedules a timer task (1000:c5d3)
at 182000 * 32 / 10000 = 582 Hz, 32 times the PC's 18.2 Hz. Each run adds `_TicAmt` (1;
`PAUSEACTION` sets it to 0) to `_TimerTics`. So game time runs at about 582 ticks a second
however fast the frames are.

**ANIMATORUPDATE** (1000:c268) runs the main list `_Update` times, the foreground list
`_Update` times and the slow list once. For each object: `Counter++`; if flag 0x40 is set,
`xPart += xSpeed` and the animator only runs when that carries out of 16 bits (so `xSpeed` is
a rate, in 1/65536 of a run per tick); then `Animator(objp)`. A nonzero return ends the
level. The next object is read before the call, so an animator may kill itself.

**Animators** are near pointers into code segment 1000: ANIMATORUPDATE (in 1000) calls them
with a near `CALL`. The decompiler shows the stored constants as DGROUP addresses:
`DAT_4a77_1987` is 0x1987 (DOBAT), and `DAT_5000_69cf` is linear 0x569cf, i.e. DGROUP
(0x4a770) + 0xc25f (NULLANIMATOR); string labels like `s_Challenge_4a77_0fe3 + 5` are 0x0fe8
(DOGARGOYLE). `tools/animators.py` lists the animator(s) SETUPOBJ gives each of the 96 object
types, from the machine code, named from the debug info. Behaviours switch animators as they
go (a statue: GENERICBEGINFALL → GENERICFALL → on landing DOCONTAINER).

**Lists.** `_MainListHead`, `_ForeListHead`, `_SlowListHead` and `_OffListHead` are singly
linked lists of objects sorted by ascending `Link.priority` (`ADDPLINKNODE` puts a new object
before the first one of equal or higher priority), so the priority is also the drawing order.
`OBJstruct.Update` is how far outside the view (in pixels) an object keeps running: 0x32 by
default, 0x1000 (always) for the foreground and slow lists. `ONTOOFFUPDATE` moves main and
foreground objects beyond that margin to the off list, where they neither run nor draw, and
`OFFTOONUPDATE` moves them back to their own list (`OBJstruct.List`) when the view comes
near.

**Objects.** `SPAWNOBJ` / `INITOBJ` allocate with `ALLOCNEAR`, which is `calloc`, so all
fields start at 0. `OBJIMAGE` sets `ImageID` and `xSize` / `ySize` (the frame's `BoxCols` /
`BoxRows`); `NEXTFRAME` steps `FrameNo` every `FrameCountMax` calls (0..`MaxFrames`,
wrapping, or ping-ponging if `AnimInc` is set) and only changes `ImageID`. Some spawners set
`ImageID` to 0 to be invisible but still call `OBJVISIBLE`, which then reads the frame header
at 0000:0000 (the interrupt table); the port uses an empty frame. `FRANDOM(n)` is
`rand() * n / 0x8000` with Borland's `rand` (seed * 0x015a4e35 + 1, bits 16-30).

**Platforms** (RXPLAT.C) move through `GenericVar[5]`: an animator sets bits 1 up, 2 down,
4 left, 8 right, and `MAINTAINPLATFORM` moves the platform a pixel each way and the player
with it if the player rides it (bit 0x20, set when the player's feet land within its top 8
pixels; the player gets `SurfaceFlags` bit 1, so CANMOVE leaves it alone). Bit 0x10 means it
can't be ridden. Pulley platforms (info `p<dir><dist><pulley>`) find their pulleys by
priority: the platform's priority is the pulleys' + 10, and they set the pulleys' `xSpeed` to
turn them. Debris (type 0x0e) jumps with GENERICBEGINJUMP and lands through DOPROJECTILE.

**The jump** (GENERICBEGINJUMP → GENERICJUMP → GENERICUP → GENERICBEGINFALL): GENERICJUMP
counts `GenericVar[4]` down and keeps `ySpeed` constant (it subtracts 0xb4 that GENERICUP adds
back); then GENERICUP decelerates. Moving up with a negative speed, the step is when
`yPart += ySpeed` doesn't carry.

## The player (RXYOU.C, RXYOUF.C)

Each `YOU*` animator (YOURUN at 1000:b5ec...) just calls its `FYOU*` worker in RXYOUF.C. The
states: BEGINRUN / RUN (on the ground; FYOUHORIZ does the walking), BEGINJUMP / JUMP / UP,
BEGINFALL / FALL, BEGINCROUCH / CROUCH (also the stun after a long fall: BEGINSTUN),
BEGINCLIMB / CLIMB / CLIMBMOVE (vines and ropes), BEGINSCRAMBLE / SCRAMBLE (over the top of a
vine), plus SHOOT (the female character's fireballs), WHIP (the male's), HIT, DIE, CRUSH,
IMPALE, DROWN. Numbers worth knowing: walking `xSpeed` 12000 (106 px/s), crawling 5000; the
jump starts at `ySpeed` -16000 and keeps it while the key is held for 0xa0 (male) / 0xc3
(female) ticks; gravity 0xb4 a tick; a fall of 0x97 / 0x74 pixels or more stuns for 180 / 240
ticks. Climbing speed -0x13aa / -0xfaa up, 8000 down.

- `GenericVar[5]` of the player: 1 on the ground, 2 falling, 4 jumping, 8 the jump key was
  released (holding it doesn't jump again), 0x10 has the special item.
- `_GameFlags`: 1 climbing, 2 attacking, 4 the male character, 8 recovering from a hit.
- Vines are tile types 0x6a (climbable), 0x6b (the top: scramble onto the ledge) and 0x6c
  (the bottom: let go). `_Vine_x` / `_Vine_y` hold the cell.
- Idle: `Counter` counts ticks standing; FYOUBOREDOM shows a sequence of fidgets from it.
  Moving sets it to 40000, which gives a short "stopping" animation first.

**Keys.** `_Keys[128]` is set by the keyboard interrupt (KBD_Int) by PC scancode;
`_KeyTrans[8]` (DS:1c34) maps the actions: left 0x4b, right 0x4d, up 0x48, down 0x50, jump
0x1d (Ctrl), attack 0x38 (Alt), special item 0x39 (Space), look 0x1c (Enter).

**Scrolling.** `SETSCROLLBOUNDS(h, w)` centres a w x h dead zone in the view;
PLAYERLEFT / RIGHT / UP / DOWN (MOVE.C) move the player and scroll the view by the same
amount when the player leaves it (SCROLLRIGHT... in 3cb4, clamped to the map). A level
starts with the view at (player x - 0x8c, y - 0x4c), a 42 x 50 dead zone and `_CenterOff`
86 (NEWLEVEL). Jumping and falling widen the zone to 140 high. The camera object (type 8,
SCREENCENTER, spawned by INITLEVEL) then scrolls a pixel at a time, about 145 a second,
until the player is between 0x4b and `_CenterOff` pixels from the top (86 standing, 102
crouching), and back to the 50-high zone. Some levels have their own camera
(CRUSHERCENTER, DRAGONCENTER, BOSSCENTER, DEMONCENTER). PROCESSLEVEL scrolls up / down while
the look key is held with up / down (or PgUp / PgDn).

**Grabbing.** After each player move, MOVE.C calls `_Grabber` (GRABSTUFF, RXGRABF.C) for
every cell entered that has bit 0x2000. GRABSTUFF looks at three tile types: the background
tile's, the bonus tile's (foreground byte bit 7 clear) and the foreground tile's (bit 7 set).
Some checks require bit 7 clear, some don't care (ported as decompiled):

| Type      | What                                                                        |
|-----------|-----------------------------------------------------------------------------|
| 10-19     | castle1 / castle2 warps to `_WarpCoords[type-10]` (`roc1warp.008` / `.009`: (x, y) words); `_quitflag` 6 |
| 0x74      | a secret: clears bit 0x2000, `_SecretFound++`                               |
| 0x71/0x72 | a gem / big gem (5): the bonus tile (on levels 11-12 the whole cell) is cleared; a DOFLYINGSHIT flies it to the counter |
| 0x73      | 1 in 100: a noise, and the bit is cleared                                   |
| 0x6d      | stakes: YOUBEGINIMPALE sinks the player to `_Vine_y` (reused), then YOUDIE |
| 0x65      | water or a bottomless pit: FYOUDROWN                                        |
| 0x67      | the exit (`_quitflag` 3), unless attacking or recovering from a hit         |
| 0x70      | back to the level's start (with a message window; `_quitflag` 6)            |
| 0x75/0x6e | spikes (foreground tiles): a free hit (`_FreeHit`); 0x6e with blood         |
| 0x68/0x69 | spawn a bolo-throwing cat / a frog (up to `_MaxMonsterNumber` at a time)    |
| fg 0x19   | level 24: starts the snake platform (type 0x44), when on the ground          |

Which warp file each castle level reads isn't explicit (READITEM takes an item number), but
castle1 uses types 10-15 and castle2 10-12, matching the 6 and 3 pairs in `.008` / `.009`.

## Damage, attacks and the inventory

**Inventory** (`_YouInventory`, DS:554b, `INVENTORY`): Score (u32), Gems, MaxHitPoints[2],
HitPoints[2], WeaponStatus[2], and a bit-field word: Poisoned1 (bit 0, the male), Poisoned2
(bit 1), WeaponPower1 (bits 2-3, the whip) and WeaponPower2 (bits 4-5, fireballs). The arrays
are indexed by `(_GameFlags & 4) != 0`: 0 the female, 1 the male. NEWLEVELSETUP copies score,
gems and the maximum hit points to `_Old*`, which a death restores. Gems are also the
female's ammunition: each fireball costs one.

**LEVELTIMER** (RXTIMERF.C) is a timer NEWLEVEL installs with a rate of 0x0c; the timer task
counts the timers every fourth tick, so it runs every 0x30 ticks (RUNTIMERS runs it at most once a
frame: about 12 times a second; earlier notes and the port had 0x24, the title's rate). It counts down `_hitagain` (the
protection after a hit; meanwhile the player bleeds drops of blood) and the player's
GenericVar[0] (the attack's recovery: 10 a run while the attack key is up, else 1), and gives
spawned objects (no `Flags` 4) parked in the off-screen list the animator OFFKILLER.

**OBJCOLLIDE** compares the frames' hit boxes (`HitCols/Rows/Xoff/Yoff`, mirrored like the
image: `x - ((HitCols - BoxCols) + HitXoff)`), x signed and y unsigned; OBJOVERLAP uses the
objects' box sizes instead. **OBJBLINK(objp, blinks, on, off)** sets Blinks, BlinkCt = DrawOn
= on and DrawOff = off. The list drawer counts BlinkCt down each frame and steps Blinks at 0;
while Blinks is odd the object is drawn all in `BlinkColor` (`_DrawColorSprite`...).

**HITPLAYER** (RXGENERL.C:416): unless `_GameFlags` 8 (recovering) or `_hitagain`, and if the
objects collide (or `_FreeHit` is set), the player loses a hit point (two when `_DiffMode` > 2),
`_hitagain` becomes 11, blood bursts where the blow lands (an explosion of kind 3) and, unless
climbing or on the easiest difficulty, `_GameFlags` |= 8, a DROPGEMSPAWNER spills up to 5
gems, and the player is knocked back: `xSpeed` 10000 against the facing, `ySpeed` -14000,
GENERICBEGINJUMP. GENERICUP / GENERICFALL call YOUHIT for priority 0xe6 (drifting sideways),
GENERICDOWN lands it with YOUBEGINSTUN (170 ticks with flag 8), and YOUCROUCH then clears the
flag and sets `_hitagain` again. With no hit points left the landing goes to YOUDIE
(`_quitflag` 4); PROCESSLEVEL counts 100 frames and starts the level again. SPIKECHECK
(RXFRSTF.C) is HITPLAYER with blood, for spikes; a spiked wall only blocks.

**The whip** (male): FYOUBEGINWHIP sets `_GameFlags` 2 and the arm's timing (`_WhippingFCM`
78 / 60 / 51 calls a frame by WeaponPower1; recovery 0x19 / 0x14 / 0x0f). WHIPPINGFRAME
shows the arm's frames 1, 2, 1. As frame 2 starts, FYOUWHIP spawns the segments (type 5,
DOWHIP), 16 pixels apart from the hand: 3 for the first whip (Sword1ID), 5 for the others,
none past a wall cell (0x200 / 0x400). `_SwordIndex` (DS:19df) holds each power's three
animation frames. A segment touching a hittable object (`Flags` 0x10, without 2) adds 3
(4 with the strongest whip) to its GenericVar[3], sets `Flags` 2 and makes sparks or blood;
DS:553a / 553b list the objects (at most 3) one crack has hit. Each behaviour checks `Flags`
2 and calls ENEMYHIT(objp, MaxHits).

**Fireballs** (female): FYOUBEGINSHOOT picks the pose (standing, in the air, climbing,
crouching; up / down held) and FYOUSHOOT throws one if a WeaponStatus slot is free and there's
a gem, then holds the pose 75 calls. DOFIREBALL (type 6) flies a pixel a run in direction
GenericVar[1] (1 up, 2 down, 3 left, 4 right; its jump table also maps 13 / 14 to falling
left / right), adds 1 to the GenericVar[3] of what it overlaps, and bursts (DOEXPLOSION) on a
wall, off screen or on a hit. Stronger fire adds one or two followers (GenericVar[2] 1 / 2,
moving 30 / 60 ticks later). GenericVar[2] 3-5 are enemies' fireballs, which hurt the player.

**ENEMYHIT** (RXGENERL.C:904): below MaxHits the object flinches (ENEMYHITPAUSE: one tick,
shoved 5 / 7 pixels away from the player but not off a ledge). Otherwise it dies: a sample by
priority, an EXPLODERSPAWNER (sparks or blood over its box for twice its height in ticks),
OFFKILLER, and a prize (type 0x14, priority 0xe5, CollectID[GenericVar[1]]): a statue's own
(its GenericVar[0]; 0x1c-0x20 are the priced magic items), else `FRANDOM(0x1b)`: 0 poison
(only on the hard difficulties), 1-4 a potion, 5-15 a coin, 16-26 bigger coins. The bosses
(priorities 0x3c, 0x47, 0x4a, 0x4c, 0x66) have their own deaths (see "The bosses"). DOCOLLECTABLE bounces the
prize once and lets the player pick it up (DOFLYINGSHIT flies it to the corner and counts it:
coins 50-1000 points, gems 1 / 5, a potion 2 hit points and the cure, poison one hit point and
poisoned); after about 1000 ticks it blinks 13 times and goes.

**Effects.** DOEXPLOSION (type 0xd): six frames of ExplodeID from 0 (kinds 0 / 2; 2 hurts),
6 (1 / 4: sparks) or 12 (3: blood); on the fifth it may start a SPLASHSPAWNER (type 0x24),
which throws GenericVar[1] + 1 bits of debris, blood, poison or sparks from the top, left or
right of its box in turn (DS:38c2). Blood drops (type 0xa) land as DOBLOOD splats. Most
invisible spawners use `Sword3ID[0]`, a one-pixel frame, as their image.

**Status display** (INITLEVEL, when `_StatusOn`): DOHITPOINTS (type 0xf, `_HitPts`) shows
HpID[base + hit points] (base 0, 5, 12 or 21 for a maximum of 4, 6, 8 or 10) in the view's top
left corner; it blinks on the last hit point and while poisoned, and poison costs a hit point
at each of the player's blinks. Four DOGEMNUMBER digits (GemNumID) and a spinning gem show
the gem count.

## Reflections, looking and the fly mode

**Reflections** (levels 8 and 9). SPAWNOBJ (main list only) and INITOBJ call PARENTLISTADD
(RXSWAMPF.C:11) for frogs (priority 0x35), statues (2), 0x34, prizes (0xe5), 0xdc, 0x36 and the
player: a type 0x5a object (DOREFLECTION, RXSWAMP.C:44, at the parent's priority through
`_ParentPriority`) recorded with its parent in `_ParentListHead` (PARENTstruct: link, parent,
child). Each run it takes the parent's x and facing, y = 2 * Rows * 16 - 0x4d - the parent's bottom
(kept within the map's last 0x26 pixels: the water), its blinking, and, through `_ReflecTable`
(DS:0d13: 14 pairs of an `_Img` set and its upside-down U_ twin, with frame counts; a 15th,
BothDeadID, isn't looked at), the upside-down frame of the parent's image. OBJKILL calls
PARENTLISTKILL, which gives the parent's reflections OFFKILLER. The port keeps the parent in the
reflection's `parent` field and finds the reflections by it.

**Looking** (PROCESSLEVEL, once a frame). While the player runs, crouches or climbs (not on levels
7, 10, 16, 25, nor while `_CenterOff` is negative), Enter with up, or PgUp, starts looking up: a
0x8c-high dead zone and `_CenterOff` 0, then the view moves up a pixel a frame while the player's
bottom stays above view y 0xa9; Enter with down, or PgDn, moves it down while the player stays
below view y 0x1e and the map goes on. Letting go sets `_CenterOff` back (86, 102 crouching), and
SCREENCENTER brings the view back. `_Looking` (1 up, -1 down) keeps the player's own up / down
from acting meanwhile.

**The fly mode** (XLEVEL, RXGENERL.C:401): holding D, CapsLock and X makes it the player's
Animator. It sets `_YouInvincible` and moves the player a pixel a tick with the arrows (PLAYERUP...,
so the view follows), through walls, within the map; the jump key lands them (YOUBEGINRUN).

## Windows, cheats and hints

**PROCESSLEVEL** (RXLEVEL.C:54) runs once a frame. In order: the level's clock (`_SecondInLevel`
/ `_MinuteInLevel`, a second every 0x247 ticks of `_TimerTics`, every fifth 0x244), the window
`_MenuComing` asked for the frame before, then the keys: Esc 0 (MAINMENU), F1 the help (`_quitflag`
5: the game is saved, the reader shown on its own level, the game restored), F3 6 (LOADMENU), F4
5 (SOUNDMENU), F8 / F9 8 / 9 (QUICKMENU), F10 10 (PROMPTWIN), F2 4 (SAVEMENU), F6 / Tab / Pause
11 (STATUSWIN); F11 the gamma (`_GammaCorrect` + 4, from 16 back to 0; SETPALETTE adds it to every
component and caps at 63); F5 the status display (debounced over 6 frames; turning it on is
`_MenuComing` 12, which respawns the hit points and gem displays); the cheat codes; every
`_NextScoreUp` points (10000, 15000 on the hardest, then each 10000) both heroes' maximum hit
points + 2 (to 10) and refilled, with a hint; the look keys and the fly mode (see below); then the
`_quitflag` cases (0 playing: SPAWNHINTS while no hint is up; 1 quit; 3 and 7, a warp, share the
level-end code, `_nextlevel` + 1).

**Windows** (RXWINDF.C and the assembly at 3f50). WINUP(col, row, cols, rows, border, ...,
top title, bottom title) freezes the game (PAUSEACTION), records the screen area (SAVEPAGE),
centres the window when col / row is -1 (`_WinX` = 0xa0 - cols * 4, `_WinY` = 100 - rows * 4),
and draws an 8x8-tile frame from a 12-frame border set (corners 0, 2, 9, 11; edges 1, 3, 5, 10;
title band joins 6, 7, 8; fill 4) with title bands 16 pixels high (`_TopOff` / `_BotOff`), then
fills the inside with frame 4 (WINCLEARTEXTAREA). Its third and first arguments are unused. The
in-level windows use Border2ID, whose frames are all empty in ROC.DAT, so they show no frame:
the text sits on the frozen game. SYSPIXELWRITE(x, y, s) writes RocFontID at (`_WinX` + x, `_WinY`
+ `_TopOff` + y): DS:1fb2 maps characters to frames, `{n}` picks font colour table n (DS:0e52, 5
bytes each: the blitter xlats each font pixel, values 0-4, through it; 14 and 15 are plain), `{-}`
goes back. FONTSTRLEN sums the frames' widths plus `_FontSpaceLetters`; MENUSTR centres. DEFAULTPAL
(`_LevelPal` colour 72 the standard yellow) picks colour table 4 or 5 and the cursor: CursorTID or
the spinning gem CollectID[18..21]. WINDOWCURSOR2(x, y) waits for every key to be let go, then
returns the scancode of the next key pressed and let go, turning the cursor at (x + 8, y + 0x18)
every 20 ticks (F11 works meanwhile; a pause ends on the Pause key too).

- STATUSWIN: MenuHeadID[0..1] (the portraits), "Endrick" / "Elandra" with their HpID icons and
  "Poisoned!", the level line ("{8}E1L3: {7}" and the name from rocdesc.txt, 30-byte slots; S for
  the secret levels) and "Gems: n   Score: n".
- PROMPTWIN(0): "ARE YOU SURE?", "Press "Y" if you really want to quit to DOS.", and one of four
  endings at random; Y (0x15) quits.
- The cheats: with Backspace held, typing PRIME, MAGMA, ASTRAL, FIRE, RAIN, WIND or ETHER
  (`_CheatCodes`, DS:02b2: 7-byte rows of scancodes) calls CHEATER(0-5) or WARPMENU. CHEATER: 0
  all of: 10 hit points for both, at least 250 gems, both weapons at full power, healed; 1-4 one
  of them; 5 invincibility on / off. WARPMENU (BorderID, which has a real frame) reads up to two
  characters with WINGETSTR; a number n of 1-27 is level n - 1 (`_quitflag` 7). The EXE starts
  `_CheatAttempt` at 0x32, which only Backspace leaves (the port starts it idle).
- The reader (F1; PAGEREADER, READERWIN, READERFETCHER): the file is pairs of NUL-ended title and
  text; a page is a 25 x 39 window with the title on top and "Page n of m PgUp PgDn (Press ESC
  to exit)" below; `{Txxx}` starts a new piece of line at column xxx, `{Pxxxyyynn}` puts InstIllID[nn]
  (CollectID[nn - 10] from 10) at (xxx, yyy); the text is centred on its widest line. Keys (a CS
  table at 2ff5:3861): Esc out, Home / End the first / last page, `-` Up PgUp Left keypad-minus
  back, `=` Enter Space Right keypad-plus Down PgDn on (Enter on the last page leaves); rocinst.txt
  wraps round. F1 opens rocinst.txt at page 12 (index 11). The original shows it on the reader's
  level, 28 ("text"), so on that picture and in its palette, and so does the port.

**Hints** (SPAWNHINTS, RXNEWLVF.C:586; DOHINTS, type 0x36, priority 0xfe). With `_MessageOn` (on
by default), PROCESSLEVEL asks for the next hint whenever none is up (`_OldGameFlags` 0x4000, set by
SETUPOBJ). On the first two levels the lessons come first (HintLernID: Esc for the main menu,
Space to change heroes, low health, the first gem, statue, prize or secret, by `_GameFlags` 0x10,
0x40, 0x80, 0x20 and `_OldGameFlags` bits for "shown"); then HintsID (the exit, extra and last
hit points, statues too dear, magic items, the special item, poison). A hint slides up from view
y 0xc6 to 0xa1 at the view's left (a pixel every 4 ticks), stays 0x579 ticks and slides away.
`_GameFlags` and `_OldGameFlags` last from level to level (NEWLEVELSETUP clears only `_GameFlags`
1, 2 and 8) and start again at the first levels of an episode (next level 0-2, 8, 9). The port
also clears a leftover "hint up" bit at a level's start, which the original keeps (and which would
stop the hints).

## Sound

The Apogee Sound System (`SOURCE\*.C`: FX_MAN, MULTIVOC, BLASTER, GUSWAVE, AL_MIDI, ...) plays
effects on `_SoundCardType` (-1 none; FX_SetVolume is the card's mixer: 0 Sound Blaster, 1 Pro
Audio Spectrum, 5 UltraSound) with `_SoundVoices` voices, and MIDI music on `_MusicCardType`.
SETUPSOUND (RXSAMPLF.C:236) starts both from ROC.CFG.

**Samples** (LOADITEMS, RXITEMSF.C): rocsound.txt is 16-byte records, a VOC file's name, its
number (byte 14; ITEMstruct.Ident) and the number of the sample to play after it (byte 15,
`Link`): bats1 -> bats2 -> bats3 -> bats1, the bubbles, the castle's screams... All 185 VOCs are
one type-1 block of 8-bit unsigned PCM, rate byte 164 or 165 (10870 / 10989 Hz).

**PLAYSAMPLE(n, source)** (RXSAMPLF.C:135): -3 without a sound card; reads the sample in if it
isn't (freeing lower-numbered unlocked ones if memory is short), adds a SOUNDstruct to
`_SoundListHead` (ADDPLINKNODE by the sample's number) with its source object and the handle
`_NextSoundListHandle` (1..0x7fbc), and plays it: `FX_PlayVOC(data, _SoundVolume, 7, n)`. The
handle is what objects keep (often in a GenericVar) for SOUNDLINKACTIVE (still in the list) and
SOUNDLINKKILL (stopped and dropped). **MAINTAINSOUND** (:64) drops the sounds that have ended (8
at most a call) and replays each one's `Link` sample under the same handle when its source is
none, in the foreground list or visible, so a chain stays "active" until it ends or its object
goes out of view. Only FDOAMBIENTNOISE (the level's ambient object, which also needs more than 3
voices) and ANIMATECURSOR call it.

**Music**: NEWLEVELSETUP fades the song out (MUSIC_FadeVolume(0, 0xe8)), stops it, reads the
level's (rocsong.txt: 13-character names, 29 levels) and plays it looped at `_MusicVolume`.
STATUSWIN pauses it; the title fades it over 1000 ms when quitting. **Menus**: OPENMENU loads
`_MenuVOC` (sample 3, played when a line moves) and `_MenuVOC2` (0xa9, a line picked), CLOSEMENU
waits for both to finish; the title plays 0xb0 or 0xb1 once (dthfem / dthmal1, the death screams; the port only with
`--title-scream`). SOUNDMENU (RXWINDF.C:415) shows two
sliders (SliderID: the track at window (0x79, 0x3d / 0x5d), the knob at x volume / 4 + 0x7d,
y 0x40 / 0x60); VOLUMESELECT moves the line with up / down or S / M, a slider with left / right in
steps of 16 (with the click; the knob drawn after a step up sits a pixel further right) and ends on
Esc or the jump key.

**The port** (`src/audio.c`, `src/sound.c`): effects are mixed with SDL2 (44.1 kHz, linear
interpolation; with no free voice the oldest of the lowest priority is taken, and all play at
priority 7); without a device the mixer is advanced by the game's ticks, so sounds last as long
and the behaviours waiting on them behave the same headless. A freed object's sounds play out
without going on (the original would read the freed object). The music: see below.

## The AdLib music

ROC's music driver for an AdLib or Sound Blaster is Apogee's AL_MIDI (segment 45b5, no debug
info: read from the machine code), fed by MIDI.C (42b4). The game never loads another instrument
bank (MIDI_LoadTimbres isn't called), so `_ADLIB_TimbreBank` (DS:25f2) is the music's sound.

**Tables** (DGROUP): the drum map at 24f2 (128 keys x (instrument, note): the drums are
instruments 128-255 played at a fixed note); `_ADLIB_TimbreBank` at 25f2 (256 x 11 bytes: the 20h,
40h, 60h, 80h, E0h registers for the modulator and the carrier, then C0h); slotVoice at 312c (each
of the 9 voices' two slots), offsetSlot at 3150 (a slot's register offset), NotePitch at 3162
(32 detunes x 12 notes of F-numbers: 343-647 in tune, A = 577), OctavePitch at 3462 (block << 10),
the reserved voices at 3472. In BSS: Channel[16] (0x1c bytes at a22a: the voice list, Timbre,
Pitchbend, KeyOffset, KeyDetune, Volume, pan, detune and two fields never set), Voice[9] (0x18
bytes at a3f2: list links, num, key, velocity, channel, pitchleft / right, timbre, status),
VoiceLevel / VoiceKsl per slot, NoteMod12 / NoteDiv12 for 0-95.

**AL_MIDI**: AL_Init (ports 388h, mono) and AL_Reset: register 01h = 20h (waveforms on), 08h = 0,
BDh = 0 (no rhythm mode), every voice's A0h / B0h / E0h cleared, PitchBendRange 2, the voices put
in a free list in order, every channel's volume 90. AL_NoteOn takes the free list's first voice
(if none, the drum channel's first voice is cut), puts it on the channel's list and sets its
timbre (unless it has it; the modulator's 40h from the instrument, the carrier's left at 3Fh), its
volume (carrier 40h = ((63 - level) x (velocity | 80h) x channel volume >> 15) ^ 3Fh | KSL) and its
pitch (note = key, or the drum's note, + KeyOffset - 12, kept to 0-95; A0h / B0h = OctavePitch |
NotePitch[KeyDetune][note % 12] | 2000h for the key). AL_NoteOff clears the key bit (B0h = pitch
>> 8) and gives the voice back to the end of the free list. AL_ControlChange: 7 the channel's
volume (its voices updated), 10 pan and 94 detune (stored for the stereo OPL3 only), 121 resets
every voice and channel (without touching the chip), 123 all notes off. AL_SetPitchBend: bend =
lsb + (msb << 8) (not << 7), t = (bend x 2 & FFFFh) / 200h in 1/32 semitones: KeyOffset = t >> 5 -
2, KeyDetune = t & 31. **MIDI.C** sends a channel's volume as volume x `_MIDI_TotalVolume` / 255
(MUSIC_SetVolume, fades); MIDI_Reset sends all notes off, 121 and a bend range of 2 to each channel
and sets their volume to 90; controller 126 skips a byte more than it should.

**The port**: `src/almidi.c` is that driver and layer; `src/opl.c` an OPL2 written for the port:
per slot a 19-bit phase advanced by ((F-number << block) >> 1) x 2·multiple >> 1 a sample, a
quarter-sine table in -log2 units (1/256 of an octave of level) and an exponent table back,
attenuation in 0.1875 dB steps (TL x 4, SL x 16, KSL from its ROM by F-number and block),
envelope rates as (rate x 4 + key scale) with the usual 13 increment patterns on a counter that
ticks once a sample, tremolo (210 steps every 64 samples, 1 or 4.8 dB) and vibrato (8 steps every
1024 samples), feedback ((out + previous out) >> (9 - FB)) and the modulator's output added to the
carrier's 10-bit phase. It runs at 49716 Hz in the audio callback, which plays the song's events
at their sample and resamples linearly to 44.1 kHz.

**Stereo.** AL_MIDI can drive two chips, one a side: `_AL_Stereo` (DS:3488) sends every
AL_SendOutput write to `_AL_LeftPort` and `_AL_RightPort` (DS:3484 / 3486) instead of 388h, and
DS:348a (an OPL3: its two register halves) has AL_Reset turn on the OPL3 (register 105h = 1 on the
right port, cleared by AL_Shutdown) and writes C0h with 20h on the left and 10h on the right (the
output bits) instead of sending it to 388h. AL_SetVoicePitch then plays each note on the right
chip 5/32 of a semitone sharp (KeyDetune + 5, carrying into the next note past 31, not past note
95; the voice keeps it as pitchright for AL_NoteOff), so the two sides beat against each other
like a chorus; volume is the same both sides, and the pan (10) and detune (94) controllers that
AL_ControlChange keeps are read nowhere. ROC.EXE never uses any of it: AL_Init (called once,
from MUSIC_Init: the far call at image offset 31bf8) clears both flags and sets both
ports to 388h, and nothing else in the EXE writes them. So the original's music is mono on every
card, the SB Pro and SB16 included.

The port plays this dormant stereo by default (the user's choice): two OPL2s as on a Sound
Blaster Pro (388h reaching both, so C0h and AL_Reset's registers go to both chips), the left at
the notes' pitch and the right sharp, each to its own speaker; `--mono-music` gives the original's
one chip on both speakers. Checked with a scratch renderer: mono is byte for byte what the port
played before; in stereo the left side is that same signal and the right a test A4 at 441 Hz
against 438 Hz, at the same loudness and without clipping; the songs' two sides correlate at
about 0.2-0.4. Two chips cost about 2% of a core.

## The game loop

**Timers** (TIMER.C and the timer task at 1000:c5d3, 582 Hz): each run adds `_TicAmt` (1, or 0
while PAUSEACTION has the game frozen) to the game's time (DS:1c54, ANIMATORUPDATE's ticks) and 1
to `_TimerTics` (DS:1c58, real time: WAITTICS waits on it), and every fourth run, while the game
runs, counts each INSTALLTIMER timer up; RUNTIMERS calls a timer whose count reached its rate.
So rates are in fours of ticks: LEVELTIMER 0x0c (48 ticks), TITLETIMER 0x24 (144 ticks).

**`_MachControl`** (RXMAIN.C, the real main): the screen, the fonts, then NEWLEVEL and
SAVECONFIGFILE until `_QuitGame`. `_nextlevel` starts at 27 (the title), or at the command line's
level after GAMEINIT. **NEWLEVEL** (RXNEWLVF.C:42): NEWLEVELSETUP returns 0 for the title (frame
hook PROCESSTITLEPAGE, TITLETIMER), 1 for a level (PROCESSLEVEL, LEVELTIMER) or 6 for level 28
(PROCESSTEXT); then RUNLEVEL with the view on the player (fixed for 15, 20, 25 and 27+, or
`_RestoreX/Y` after a restore), again while `_RestoreFlag` is 0xfe; then everything is freed.
**NEWLEVELSETUP** (:155): stops the music, clears the level's state (`_GameFlags` but 0xfff4,
the found counts, `_quitflag`, `_Vine_x/y`, `_hitagain`...), keeps the score, gems, hit points and
`_NextScoreUp` in `_Old*` for a death, reads a restore's level (RETRIEVELEVEL: 'd' means none), shows
`loader.vga` (DISPLAYVGA + FADETOPAL) for levels below 27, loads the level (on `_DiffMode` < 3
levels 1, 3, 4, 5, 6, 11, 12, 13, 21, 22 and 24 use frsteasy, riveasy, caveeasy, casteasy,
cas2easy, musheas1, musheas2, cavebeas, cave1eas, mushseas and gobseas.led, and level 26
secreasy.led on Super Easy), the warp and palette tables, the song (rocsong.txt), counts the
level's gems (tile types 0x71: 1, 0x72: 5, background or bonus tiles), secrets (0x74, one fewer on
level 18), statues (placed hittable objects of priority 2) and enemies (the other hittable ones),
then RESTOREGAME or INITLEVEL. **RUNLEVEL** (3cb4:157c, assembly): `_Transition1` (MYTRANSIT1:
fade out and blank, unless the title or `_SkipFade`), draws both pages, `_Transition2`
(MYTRANSIT2: fade to `_SavePal` after a restore, else `_LevelPal`; `_ScreenOn` = 1), then per frame
the frame hook (non-zero ends the run), RUNTIMERS, ANIMATORUPDATE and the drawing. MYFADEOUT /
FADETOPAL take 32 steps of a retrace and a tick, each step 1/(32 - n) of what's left (FADETOPAL's
maths is 16-bit unsigned: a step down wraps round to its low byte).

**PROCESSLEVEL** `_quitflag` cases (RXLEVEL.C): 1 `_QuitGame`; 2 back to the title; 3 and 7
`_nextlevel` + 1, the hit points topped up, and for 3 PERCENTWIN, and at an episode's end (next 8,
17 or 26) FINALPERCENTWIN, HS_CHECKSCORES(episode) and the story (roccast, rocgobl, rocdemon.txt)
on level 28; the secret level (26) returns to `_ReturnLevel`; 4 (dead) counts 100 frames in
`_QuitGame`, then QUICKMENU(2) (DEATHRESTOREWIN, if the quick save holds this level) or the level
again with the `_Old*` score, gems and hit points; 5 (F1, or the menu's "Read Me!") saves to ROC.SGT
and goes to level 28, whose PROCESSTEXT reads the file and then restores ROC.SGT (`_RestoreFlag`
'E': "ROC.SG0" with its digit + 'E' - 1 is "ROC.SGt"); 6 RUNLEVEL again (a warp within the level).
The windows `_MenuComing` asks for open at the next frame; saving (F2 / F8) isn't allowed while
dying or between levels.

**The title** (RXTITLE.C:11 PROCESSTITLEPAGE): Esc / Enter / Space MAINMENU, F1 the help, F3, F9,
F10; after 0x3d TITLETIMER runs (about 15 s) text.pal / text.bg replace the title's palette and
the first 260 tiles (`_BgBuffer`, `_Bg2Buffer`), RUNLEVEL runs again without fading, and
DISPLAYCREDITS (3000 ticks) and DISPLAYSCORES (the Hall of Heroes' three pages, 1500 ticks each)
show; then title.pal / title.bg come back. A new game (DIFFICULTYMENU: `_nextlevel` 0xff, 7 or 0x10
and `_quitflag` 7) or a load ends the title's run because `_nextlevel` or `_RestoreFlag` changed;
the branch that sets `_QuitGame` is only reached with `_quitflag` 1. **PROCESSTEXT** (:234) shows
a page a frame (PAGEREADER); after an episode's story ENDWIN ("READY FOR MORE?") starts the next
episode (GAMEINIT, level 8 or 17); after rocdemon.txt the Hall of Heroes, then the title.
**GAMEINIT** (RXWINDF.C:210): no score, 100 gems on Super Easy, hit points 10/10, 6/8 or 4/6 (the
male 4 on Psycho Warrior), weapons at 3, 2 or 1, `_NextScoreUp` 10000 (15000 on Psycho Warrior),
`_GameFlags` 4 (the male), `_LightningFlag` 1 (the hero arrives in lightning), the totals cleared.

**Menus** (RXWINDF.C): OPENMENU draws the lines in BigFontID 16 pixels apart in a centred
Border2ID window (whose frames are empty: the menus are text on the picture); SELECTMENU moves
with up / down or a line's first letter (LineKey), picks on Enter or attack being let go (1-based),
leaves on Esc or jump (100), flashes the selected line's outline colour (font colour table 0 or
1, entry 0) a step every 20 ticks and remembers each menu's line (DS:53e0). MAINMENU (title: New
Game, Options, Load, Hall of Heroes, Read Me!, Quit; level: New Game, Options, Load, Save, Read
Me!, Exit to Title, Quit), OPTIONMENU (Heroes' Status in a level, Messages, Display, Blood
behind the blood password, Blood Password: old, new, again, Sound Volume), SAVEMENU / LOADMENU (8
slots ROC.SG0-7: "ROCX", version 'd', a 20-character description, the level; an empty description
is "* n *"), QUICKMENU (the last slot used, `_QuickFile`), PROMPTWIN ("quit to DOS.", "end this
game.", "restore", "QuickSave over", "QuickRestore"). PERCENTWIN shows the time against the level's
best (roc.sco), the gems, enemies, statues and secrets found of the level's, and adds them to the
totals FINALPERCENTWIN shows. **roc.sco** (HSCORE, 594 bytes; rocscore.txt in ROC.DAT is the
default): per episode a 24-character name and the score, the time (h:m:s), enemies, statues, gems
and secrets, and each level's best time. HS_CHECKSCORES asks for a name once (HS_GETNAME; empty is
"I forgot my name!") for every record equalled or beaten. **ROC.CFG** (CONFIGstruct, 70 bytes):
`_KeyTrans[8]`, MyBUTTON0-3, the joystick's calibration, the sound and music cards, voices and
volumes, StatusOn, MessageOn, JoystickOn, BloodOn, BloodPass[20], GammaCorrect.

**The port**: the windows block on `host_frame` as the original blocks on WAITTICS; WINUP keeps
the screen (SAVEPAGE), WINCLEAR puts it back (REFRESHPAGE redraws the level), WINDOWN leaves the
window for the next frame. The port saves games in its own format (`ROCP.SG0-7`, `ROCP.SGT`): the
same header ("ROCP", version 1) and every object and the game's state (pointers as object numbers,
images as `_Img` indexes, the maps and palettes, `_DragonMove`). It also reads and writes the
original's `ROC.SG0-7` (see "The original's saved games").

## Bolo cats and the snake platform

**Bolo cats** (RXFRSTF.C; types 0x27 cat, 0x28 bolo). GRABSTUFF's bonus tile type 0x68 (levels 1, 2
and 6), while `_MonsterNumber` < `_MaxMonsterNumber` and `_MonsterCounter` is 0, drops a cat at the
view's top above ground found by FINDBOLOPOS(dir) (RXGRABF.C:206: columns 16 pixels apart from
view x 0xf0 leftwards or 0x3c rightwards, stopping 0x37 pixels from the player; each ground cell
(floor bit or slope) between view y 0x50 and 0xc0 taken 1 time in 3; the original drops a place
found in the last column). Like the frog tile it stops after three (DS:38da). The cat (OF_RATE
0x4268, hittable, counted in `_MonsterNumber` through SurfaceFlags 8) falls (BOLOCATFALL), winds
up and throws: on level 6 BOLOCATCOCK2 throws shuriken (ProjectiID; a second rising one above
difficulty 2); elsewhere BOLOCATCOCK whirls a bolo in its hand (type 0x28, GenericVar[0] 0: it stays
while any cat lives, up to 0x227 ticks), then BOLOCATTHROW after 0x1c2 ticks throws at frame 3:
straight on difficulties 0, 1 and 4, aimed (GenericVar[1]: 1 rising, 2 falling) on 2-4, stands and
at 1000 ticks leaps back out of the view's top (BOLOCATFALL with GenericVar[0] 0) to come down
elsewhere, on the other side, or goes. A thrown bolo (OF_RATE 0x6978) flies a pixel a run, a
pixel up or down every second run, until a wall. A hit takes 2 points (4 while throwing after
level 3).

**The snake platform** (RXPLAT.C:1065 DOSNAKEPLAT, type 0x44, level 24 "goblins"). Foreground tile
0x19 under a player on the ground starts it at one of two places (by the cell's row 93 or not):
the head's cell is the object's x / y (cell coordinates, not pixels) and the tail's GenericVar[1]
/ [2]. The body is foreground tile 4 and the track ahead tile 0x18; at every fourth run (OF_RATE
0x7d0) the head moves onto the next track cell, which gets the floor and wall bits, and the tail
moves along the body; the four runs animate the head (0x41, 0x40, 5, 4) and the tail (5, 0x40,
0x41, then cleared, its cell's bits removed). At the track's end the head stays (GenericVar[3]);
with no body left the object goes.

## The special item and the magic gems

**The special item** (key 6, Space; RXYOUF.C:1494 FDOTRANSFORMER). The player's GenericVar[5] bit
0x10 is the item; FYOUHORIZ, FYOUCROUCH and FYOUCLIMB check the key (not while attacking or
recovering) and spawn type 0x37 (OF_RATE at 5000, priority 0xc8), whose SETUPOBJ takes the item
away. At its first run lightning strikes: DOLIGHTNING bolts (type 0x0c, LightngID, kept where they
struck relative to the player for 30 ticks) every 0x20 pixels from the view's top, and 200 ticks of
sparks (an EXPLODERSPAWNER). 100 ticks later the hero changes: `_GameFlags` 4 flips, and with
it `_YouWalkID`..., the hit points, the weapon and the attack key's meaning. The player blinks in
colour 0x1d throughout; after 800 ticks the item is back. `_LightningFlag` (set by the warp
menu, not ported) reuses it: INITLEVEL starts a warped-to level with one (> 0: the player
appears), and < 0 takes the player away to `_nextlevel` (`_quitflag` 8).

**The magic gems** are statues' priced prizes (CollectID 0x21-0x29: a statue's number 28-32),
taken when the player has enough gems (DOCOLLECTABLE) and started by DOFLYINGSHIT:
- 0x21 (25 gems): five shields, SHIELDGEM (type 0x55, ProjectiID), spread round `_Circle1` from the
  offsets at DS:00c0 (0, 54, 106, 160, 212). GenericVar[0] is the place on the circle and
  GenericVar[1] / [2] the offset from the player (16 pixels higher while crouching); a step round at
  each carry of 0x4000, backwards while the player faces left. A shield flies to its place a pixel
  at a time (jumping there when more than 0x37 off), then sticks (GenericVar[3]) and strikes
  hittable things it touches (a hit point, sparks or blood), twice (GenericVar[4]); all go when
  the female's hit points are gone, and none strike while `_hitagain` is negative.
- 0x23 (15 gems): LIGHTNINGGEM (type 0x56, OF_RATE 0x400): `_hitagain` held at -11 (LEVELTIMER
  counts negative values up, and HITPLAYER refuses while it's nonzero) for 0xddd ticks while the
  palette steps by 5 to its negative (63 - `_LevelPal`), then back.
- 0x25 (25 gems): DEATHGEM (type 7, OF_RATE 0x400): every palette component set to 60, fading 3 a
  run back to `_LevelPal`; at every run until then each hittable creature in view (not a statue)
  gets GenericVar[3] 5 and ENEMYHIT(4), with blood off. That includes the bosses.
- 0x29 (50 gems): both weapon powers up a step, and SWORDGEM (type 0x59, OF_RATE 0x1770) freezes
  the player (NULLANIMATOR, the old animator kept) glowing in colour 0x4a (the blink restarts at
  each run) with `_hitagain` at -11 while sparks (POWERUPSPARKS) fly in from the view's edges, for
  0xc00 ticks; at 0xf00 the player moves again.
The gems' palette code ends by setting `_SavePal[0]` to 100, which isn't ported.

## Enemies

Notes on the creatures ported after the forest ones. Many run with `OF_RATE` (flag 0x40), so
their animator runs once per carry of `xSpeed` while `Counter` still counts ticks. `Flags` bit 0
is a per-behaviour direction bit for several of them (fireballs, spiders, puffs, poison:
DOPROJECTILE moves left only when both bit 0 and `xDir` are clear).

- **Gargoyle** (type 0x1d, RXFRSTF.C:526). FDOGARGOYLE is a sparse `switch (Counter)`, which
  Borland compiles as a search loop over a table of values followed by a table of targets
  (`MOV CX,5; MOV BX,0df2; CMP AX,CS:[BX]; ... JMP CS:[BX+0Ah]`). Ghidra decompiles it as a call
  through `puVar2[5]` and leaves the case code undisassembled, so it was read from the raw image.
  Ticks 1, 0x191, 0x321: if visible, spit a fireball (type 0x24 with Animator DOGARGOYLEFIRE,
  `GargFireID`, xSpeed 16000) towards the player; 0x7d0: blink; 0x802: `Counter` = 0. The mouth is
  3 or 21 pixels down depending on the carving (`GargoyleID[1]`, level 4) and the side.
- **Flame vents** (type 0x33, FDOCAVEHOLE): info 'r' / 'l' / 'd' / other. Between ticks 1500
  and 2000 every run spawns a DOTORCHPUFF (type 0x24): six frames of `ExplodeID[6..]`, drifting,
  burning while `GenericVar[0]` is set. Mud men on level 26 (lava) shed the same puffs.
- **Spiders** (type 0x1e, FDOCAVESPIDER) write their thread into the foreground map: tile 0x4f
  at each cell they enter going down (MAPUPDATE redraws it), 0 climbing back. They spit poison:
  type 0x2f, a GENERICBEGINFALL with DOPROJECTILE kind 3.
- **Falling spikes** (type 0x1c, DOCAVEFALLSPIKES) rattle (sample 0x53, jiggling a pixel)
  within 64 / 32 pixels and drop within 32 / 16 (the hard difficulties halve it); the
  Animator they get, `_Img.GemNumID + 0x1b`, is 0x415b, GENERICBEGINFALL.
- **Boars** (type 0x51, RXSKELF.C): Boar1ID with a spear (xSpeed 0x1060), Boar3ID breathing fire
  (xSpeed 8000; the speed doubles as the kind). A boar wholly in view at the player's height
  attacks from more than 60 pixels with a spear (type 0x52, priority 0x3e: GENERICBEGINJUMP,
  GENERICUP / GENERICFALL call FDOBOARSPEAR, landing on DOBOARSPEAR) or three fireballs (type
  6, GenericVar[2] 3-5); a spearless boar (Boar2ID, GenericVar[0] 1) charges from under 30.
- **Goblins** (type 0x3b, RXGOBLIN.C; fists before level 14, clubs after): stand and turn every
  300 ticks; after three turns facing the player they run (17000 / 20000 a step), punch within
  reach (a type 0x3f GOBLINFIST hit box that lasts 300 / 100 ticks), and jump off ledges, where
  GOBLINJUMP (priority 0x33 in GENERICUP / GENERICFALL) steers them and stakes kill them. The
  speeds read as `0x4268` and `_SavePal + 0x50` in the decompile (both are constants: 17000 and
  0x4dd0 + 0x50 = 20000); `_SavePal + 0x23d` is ENEMYHITPAUSE.
- **Mud men** (type 0x3c, RXSWAMPF.C) start 8 pixels in the ground and rise while CANMOVE says
  they could fall. MUDMANHIT adds a point of its own to GenericVar[3] and throws mud (type 0x38,
  which SETUPOBJ sends to case 0: a GENERICBEGINJUMP with random speeds); 10 points kill (60 for
  the lava men).

**Platforms that block** (RXPLAT.C:925 MAINTAINBLOCK). Solid objects call it when they
overlap the player and aren't being ridden: falling bricks, square platforms, boulders, the
goblin boss's platform (FLOATINGPLATFORM when it's `_Boss`) and spiked walls (SPIKECHECK, for a
player walking or crawling into them). From below, a block stops a jump; the player drops a
pixel if there's room, crouches if standing under a block that isn't moving sideways, and is
otherwise crushed (YOUCRUSH). From the side it pushes the player a pixel (following slopes),
and crushes a player against a wall if it's moving that way (always on levels 16 and 25). It
checks `HitPoints[0]`, the female's hit points, whichever character is playing.

**More creatures.**

- **Falling bricks and crushers** (DOFALLINGPLAT): Flags bit 0 means falling. A brick (under
  18 pixels high) shakes (BRIKSHAKE) while the player is near and falls when the player is
  right under it; after a bounce it becomes part of the map, setting the floor and wall bits
  (0x800, 0x200 / 0x400) of its two cells. A crusher (a cave block whose info isn't "s..")
  rises back 500 ticks after each landing and falls again. Square platforms ("s<side>[r]")
  go round a 64-pixel square, a pixel every 16 ticks.
- **Statue bombs** (info "b", DOCONTAINERBOMB) go off when the player, a flame puff or a spore
  touches them, or when hit. BOMBEXPLODER (RXCASTLE.C) is the castle's bomb explosion: 9
  DOTORCHPUFFs; the three blobs thrown with it are type 0x48, the same as spores.
- **The raft** (SWAMPRAFT) keeps its front piece (type 0x5b, drawn over the player) in
  GenericVar[0] as a near pointer; the port keeps it in a `child` field.
- **Frogs.** GRABSTUFF's tile type 0x69 spawns a frog (type 0x3e) in the foreground list
  50-150 pixels to one side of the player, at the bottom of the view; FROGLAUNCH makes it leap
  (ySpeed -20000). When it starts falling, GENERICBEGINFALL makes a copy in the main list (so
  it's drawn behind the water's foreground tiles again) and gives the original OFFKILLER. It
  lands on land (DOFROG: croaks and hops) or in the water (a splash, and it goes). DS:38da
  counts the spawns from a tile: the third (or the first on the easy difficulties) clears the
  tile's 0x2000 bit. The giant cave frogs are platforms (FROGPLAT).
- **Mosquitoes** (DOMOSQUITO) fly arcs from a 21-byte step table at DS:0cfe
  (5,3,2,2,1,2,1,0,1,1,0,1,0,1,0,0,1,0,0,0,0), forwards or backwards, and sometimes dive at
  the player along a line stored as sixteenths of the distance. They keep their buzz's sound
  handle in FrameCountMax.
- **Spores** (DOSPOREHOLE, DOSPORE, RXMUSH.C): a hole cycles through 14 runs, spitting a spore
  at the third. Spores burst on walls, on the player, or on landing.
- **Rock monsters** (RXGOBLIN.C) come in three sizes (GenericVar[0] 0-2, RockMon1-3ID) that
  take 40, 32 and 24 damage points. A big one breaks into two medium ones, and a medium one
  into two small ones (type 0x3a, GENERICBEGINFALL with ROCKMONSTERFALL rolling them in the
  air); a small one crumbles. Every hit chips off rock pieces (type 0x38, RockPiecID). The
  `FRANDOM` ranges in ROCKMONSTERHIT are lost in the decompile and had to be read from the
  machine code.
- **Drips** (DODROP): an invisible tap drops water (1 in 4000 a tick); the cat skeleton
  (DOCATSKELETON) walks and falls off ledges like the other walkers, takes 12 points, and dies
  (ENEMYHIT, priority 0x4e) into its eleven bones: type 0x38 debris with BonesID[i], at x +
  FRANDOM(width), y + FRANDOM(height / 2), facing FRANDOM(2).

**The castle, the wisp and the chains.**

- **Knife throwers** (type 0x35, DOKNIFEGUARD, OF_RATE). Every 0x79 ticks of its Counter,
  GenericVar[0] counts a step: at 2 the routine restarts (1 in 4) or skips to 5 (1 in 4 of the
  rest; the `FRANDOM(4)`s are inlined as `rand() << 2 / 0x8000`), at 6 the arm goes up, at 7 it
  throws and at 8 it comes down. The step jump table (`JMP CS:[BX+068b]`, on GenericVar[0] - 2)
  was read from the EXE. The images are set straight into ImageID, not with OBJIMAGE, so the
  box stays frame 0's. It backs away (the walk played backwards: AnimInc -1) from a player
  nearer than 100 pixels and walks to one farther than 0x74, only where the floor bit (0x800)
  of the cell under its leading corner is set. The knife (type 0x24 with Animator DOGUARDKNIFE,
  xSpeed 0x4000) keeps `|dx| / (y - (player y + 8))` in GenericVar[0], an *unsigned* division:
  from above the player it is 0 (straight), from below it climbs a pixel every that many, and
  level with the player the original divides by zero (the port throws straight).
- **Bomb throwers** (type 0x30, DOBOMBGUARD) throw at Counter 0x4b1 and rest until 0x579. The
  bomb (type 0x31: GenericVar[4] 0x5f, ySpeed -4000, priority 0x3a, GENERICBEGINJUMP) is DOBOMB
  in the air too: GENERICUP / GENERICFALL call it for priority 0x3a, so it keeps flying
  sideways at the thrower's 16000. Landing sets GenericVar[5] bit 0, which (like touching the
  player) sets it off: it becomes an invisible BOMBEXPLODER.
- **Back stars** (type 0x16, DOBACKSTARS) hold their place in the view, not the map, so they
  look infinitely far away, and show only where the background tile's pixel under them is
  colour 1 (read from the tile in Mode X plane order: `(x & 3) * 64 + (y & 15) * 4 + (x & 15) / 4`).
  On castle1 / castle2 they are clamped to the part of the map with windows.
- **The wisp** (type 0x5e, DOWISP, RXDEAD.C) runs its own rate (xSpeed 10000) and steps
  round `_Circle1` (468a:0000, a far segment: 135 signed (dx, dy) byte pairs, then "cc"),
  using GenericVar[2] as a near pointer into it; the steps' x sum is -1, made up by an extra
  pixel at each lap. Its four lights (two swinging sideways 10 px, two up and down 12 px) are
  more wisps (GenericVar[1] 1 / 2) holding the wisp's pointer in GenericVar[0]; GenericVar[1] 3
  are the sparks. Within 160 x 75 pixels it sets `_WispWalk` (DS:19d9), which the player's
  FYOURUN / FYOUHORIZ obey like held arrow keys: the player walks to the wisp.
- **Chains** (type 0x42, DOCHAIN, RXMUSH.C) are climbed like vines with `_Vine_x` 0: up or down
  while touching one starts YOUCLIMB directly, and GenericVar[5] bit 0x20 marks the chain
  held. Each run it pulls the player a pixel back to 3 pixels off its x; leaving every held
  chain (priority 0x15) is YOUBEGINFALL. On goblin2 (level 23) the chain and its end (type
  0x43, DOCHAINEND) hang from `_Boss`, the goblin boss's platform, at x + 0x10. On level 15
  they are NULLANIMATOR pieces of the crusher.
- **Ambient noise** (type 0x5c, FDOAMBIENTNOISE) plays the level's background sample again
  whenever the last has ended, a sample per group of levels (`do_ambient_noise` in
  `src/rxyou.c` has the table). It does nothing on a sound card with 3 voices or fewer.

## The bosses

**Shared parts.** `_Boss` points at the boss (INITOBJ / SETUPOBJ set it). Its GenericVar[3] is
the damage taken, and its hit points come from a table at DS:00c6: five bytes per `_DiffMode`
(0-4), for Ross (level 7), the dragon (10), the goblin boss (16), level 20's and the demon (25):
70 / 20 / 50 / 15 / 60 on the easiest, 170 / 30 / 120 / 20 / 110 on the default difficulty 2.
ENEMYHIT never cuts a boss's hits to 1 on the easiest difficulty, and gives each boss its own
death. BOSSHITPOINTS (type 0x34) shows BossHpID[10 .. 1] by tenths of the damage, [0] when
beaten, blinking on the last step. ENEMYHITPAUSE keeps the player inside the view while the
boss flinches. ROSSSHAKE (an invisible type 0x18) is the bosses' screen shake: up a pixel at
tick 20, down at 40, twice, the camera let go meanwhile (`_CenterOff` -1).

**Ross** (castleb, level 7, RXROSSF.C; the ROSS* animators in RXCASTLE.C call FROSS*). He
waits until `_OrgX` is exactly `Cols * 16 - 0x141`, which the view passes on its way to its
last position (`_MaxX - 320`, `_MaxX` being `Cols * 16` from VIEWPOSITION). Then the dead zone
becomes the view's width, the bar appears, and ROSSCRASH (OF_RATE, 7 runs a second) shakes the
view and drops a ceiling stone each run (type 0x1a, priority 0x3b: ROSSPIECE drifts it; it
bounces once and goes) for 0x9c4 ticks before he falls in. From then on the player is kept
right of `Cols * 16 - 0x139`. Flags bit 0 is the side he heads for; GenericVar[0] steers the
routine (0 falling in, 1 after a jump, 2 must jump next, 1-3 the lunge's stages); landing
(GENERICDOWN, priority 0x3c) is ROSSATTACK, which puts him at x 0xb9 or 0x188. On the right he
throws (ROSSTHROW by Counter: knives at 600, five times, looping back to 0x65); on the left he
lunges (ROSSLUNGE: crouch, thrust, run right past 0x10b, unhurt while running, and jump); 1 time
in 4, or after throwing, he jumps across (GenericVar[4] 0xbe, ySpeed -26000; ROSSJUMP moves him
22000 a tick sideways). His sword (type 0x18, ROSSSWORD) follows his image and goes when the image
has no sword. Images are mostly set straight into ImageID, so comparisons like "ImageID ==
RossJumpID[1]" (just landed) drive the states. ROSSDIE hides him 4 ticks in 10 (keeping the
image at DS:50d2) with an explosion every 10 ticks, 200 in all; ROSSCLEAR wins the level 1000
ticks later if `HitPoints[0]` (the female's) isn't 0.

**The dragon** (swampb, level 10, RXSWAMPF.C). Its body is part of the background; the head
(type 0x4a, priority 0x66, `_Boss` and `_Neck[0]`) and four necks (type 0x4b, priorities 3-0,
`_Neck[1..4]` at DS:50ea, made by INITLEVEL) are objects. The camera (DRAGONCENTER) freezes the
player (Animator NULLANIMATOR, the real one in OldAnimator) once the view passes x 0x28, pans
a pixel a tick to x 0xbe, then hands over to SCREENCENTER and shows the bar. The head waits for
that `_OrgX` and keeps the player between x 0xc3 and 0x142. It bobs with two little state
machines (GenericVar[0] vertical, GenericVar[1] horizontal: pick a way, accelerate by 0x78,
cruise until a 1 in 350 / 400 chance, decelerate) inside x 0x14b-0x172, y 0x3b-0x62, and its
GenericVar[4] is a fifth of its distance from the body at x 0x193, by which the necks space
themselves. Its FrameNo indexes `_DragonMove` (DS:50f4: 4 x 180 signed bytes; the decompile
writes `_SavePal + 0x270 + i * 0xb4`, which is `_DragonMove[i - 1]`, and `_ParentPriority[i]`,
which is `_Neck[i - 1]`): it records its y move each run there, and neck i, once neck i - 1's
index reaches 90, replays `_DragonMove[i - 1]` and records it in `_DragonMove[i]`, clamped
near its neighbours. At Counter 200 (0x23a on the other difficulties) DODRAGONSPIT rears up
and lunges, then spits a fireball (type 0x4c); its burst (DODRAGONBOOM, ChimBoomID) spawns frogs
like GRABSTUFF's tile 0x69 and restarts the head's Counter. FrameCount holds the roar's sound
handle. Beaten (ENEMYHIT, priority 0x66), the head jumps away (ySpeed -10000, xSpeed 0x4000)
and DRAGONHEADDIE bounces it once; the necks see the boss's GENERICBEGINJUMP / GENERICJUMP
Animator (`_Img.ChainID`, `_Img.DemSideID + 8`) and turn to DRAGONNECKDIE, the first neck
bobbing faster and bleeding while the others follow it. 2000 ticks after the head lands the
level is won (again checking `HitPoints[0]`).

**The crusher** (mushromb, level 15, RXMUSH.C). Not a creature: the shaft is 30 tiles (480
pixels) wide and the room slides across it. The camera object (type 8, CRUSHERCENTER) moves
between x 0 and 0x80 at a rate that grows by 5 a tick to 0x1000 (starting with a pause:
GenericVar[0] 1), stops 0x20 runs at each end with a thud and the view shaking, and the view
scrolls a pixel a tick after it. MAINTAINSTUFF pins the walls (type 0x40, priority 10; at the
view's left edge, or 0x130 / 0x138 right of it by GenericVar[0]), the platforms (priority
0x14, at 0x108, solid through MAINTAINPLATFORM / MAINTAINBLOCK) and the chains (0x15 / 0x16, at
0x120; DOCHAIN still runs for them) to the view. While the room moves, MOVESTUFF carries
everything in the air (GenericVar[5] bit 0 clear) along. Every run, any main-list object
overlapping a wall (left of view x 0x12, or right of 0x130) is pushed a pixel back in; the
player is crushed (FYOUCRUSH) when rock (CANGOLEFT / CANGORIGHT) or the other wall leaves no
room, unless climbing.

**The goblin boss** (goblinb, level 16, RXBOSSF.C). The room is two views wide (33 columns);
the head (type 0x4d, priority 0x47, `_Boss`) sits in the right wall (GenericVar[0] 1) or the
left (0) one, the wall's rock body being map tiles. BOSSCENTER lets the view go when the player
first lands, waits 0x44c ticks, scrolls a pixel every 16 ticks to the head's side and starts it
(BOSSHEADSPIT) once it's wholly in view. Its decompile runs into the C startup code halfway:
Ghidra resolved the function's near jumps into segment 1000, so those targets had to be
recomputed from the jump bytes (target = next instruction + displacement, same segment). The
head can be hurt only with its mouth shut (BossHeadID[0]) while spitting, and at any time while
shaking, bouncing, floating or returning; the whip can't reach it inside the wall, whose cells
are solid except the mouth's corner. Spit (type 0x4e) flies at `|dx| * 0x80 + 6000` and
splashes into three bits (type 0x57). The seventh round is BOSSHEADSHAKE: GenericVar[0] flips,
and the head jumps out (GENERICBEGINJUMP, ±32000) into BOSSHEADBOUNCE, which bounces it at the
player, each landing spawning a BOSSSHAKE (`_DiffMode` + 4 rocks from the ceiling at y 0x60),
until it comes down at y 0x72 near its new wall (x 0x40 or 0x1ae). BOSSHEADSPAWN raises a
SPINNINGPLATFORM every 0x276 ticks at the free one of y 0xf8 / 0xd6 / 0xb4 (DS:00aa: the
"taken" bytes, DS:00ad: the heights) and kills rock monsters; with the third it floats
(BOSSHEADFLOAT; its leftward clamp sets ySpeed by mistake). The platforms shuttle between the
wall and the view's edge and stop after three trips (NOSPINPLAT); the top one at rest sends the
floating head home when it passes x 400 or 0x54. BOSSHEADRETURN sinks it to y 0x80, shakes the
view six times (rock monsters possible above difficulty 2) and rewrites rows 11-16 of columns
3-9 or 23-29: FgMap gets the wall's bonus tiles (`x + y * 20 + 6`, or `+ 13` on the right), the
background cells get the four collision bits, and the other side is cleared. The decompile
shows these as `((FGMAP *)_FgMap)[1].Table[i]` and `((BGMAP *)_BgMap)[0xc].Table[i - 1]`, which
fold the column offset into a struct index (FGMAP counts as 3 bytes there, BGMAP as 4); the
machine code gives columns +3 and +23. ENEMYHIT (priority 0x47) gives BOSSHEADDIE: every 20
ticks a flicker, rocks and an explosion; after 0xb4 the level is won, and falling platforms
(SPINPLATDIE) shatter.

**The skeleton boss** (deadb, level 20, RXSKELF.C; the SKEL* animators in RXDEAD.C call FSKEL*).
INITLEVEL spawns two type 0x53 objects (SKELWAIT, priority 0x4a, xDir 1) at (0xef, 0x11e), under
the floor: the skeleton (`_Boss`) and its head (`_Neck[0]`, Animator DOSKELHEAD, GenericVar[0] 1,
hittable). The skeleton itself isn't hittable: only its head hurts it. SKELWAIT lets the camera
go vertically (`_CenterOff` -1, a 200-high dead zone) and waits for `_OrgX` = `Cols * 16 - 0x141`
(0xcf), then shows the bar and makes the dead zone the whole view. SKELRISE leaps out
(GENERICBEGINJUMP: ySpeed -30000 for 0x136 ticks, SkelHackID[1]; GENERICBEGINFALL gives it
SkelHackID[2]) and throws up a rock (type 0x54, priority 0x4b, DeadRck4ID) every 10 ticks while
rising; GENERICDOWN lands it in SKELWALK facing right or SKELLUNGE facing left. SKELWALK steps 9
pixels a frame until within 0x5a of the player, then SKELHACK swings four frames 8 pixels apart;
SKELLUNGE glides left at 18000 with the sword out until x 0x122. Both set `_FreeHit` for a player
in front of the blade from the third frame. SKELSINK sinks 2 pixels and a rock every 12 ticks to y
0x104, then it hides at y 0x11e on the other side (x 0xef or 0x1d3, xDir flipped) for 300 ticks
(SKELWAIT2) and rises again. All of them keep the player right of `_OrgX + 5`.

The head shows SkelHeadID[0..4] by its distance from the player and, on the neck, is placed by a
chain of comparisons with the skeleton's image (for the hack's images, facing right, the original
subtracts its 5 pixels twice). A weapon hit knocks it off (GenericVar[0] 0) away from the player
(GENERICBEGINJUMP, ±32000, -16000) and arms it (GenericVar[1], blinking, 500 ticks); it then
bounces for good (each landing reverses ySpeed, nudged towards -27000, and xSpeed decays to 16000)
off the view's sides (`_OrgX` .. `_OrgX + 300`), and hurts above difficulty 1. An armed head
overlapping the skeleton (OBJCOLLIDE) is a hit: the skeleton's Flags 2 and GenericVar[3] + 1, a
blink, sparks; its next animator call is SKELHIT (4 bones, type 0x38 with DOPROJECTILE kind 7,
and ENEMYHIT with the hits at DS:00c9). The check runs in the same call as the knock-off, so a
head struck while on the neck can hit the body at once. ENEMYHIT (priority 0x4a, `_Boss`) gives
SKELDIE: the skeleton floats up to `_OrgY + 0x14`; every 45 ticks (GenericVar[1]) a round of
sounds, two explosions and four bones (first 60 rounds), hidden for 25 ticks with the view down a
pixel (first 45); after 71 rounds `_quitflag` 3. The head sees SKELDIE and dies too (ENEMYHIT with
GenericVar[3] 3, `_EnemyFound` taken back).

**The demon and the ending** (demon, level 25, RXDEMONF.C; the DEMON* animators in RXDEAD.C call
FDEMON*). The view starts at (0x50, 0x50) (NEWLEVEL: levels 15 and 27+ start at (0, 0), level 20
at (0, 0x60)), and the camera, DEMONCENTER (type 8, its own rate at 22000), keeps the dead zone the
whole view, pulls `_OrgY` back to 0x50 and keeps the player between `_OrgX + 10` and `+ 300`; above
difficulty 1 the ground at x 0xbe-0xd7 hurts. The demon (type 0x5f, priority 0x4c, `_Boss`; DemStrID
straight on within 0x33 pixels of the player, else DemSideID) is only a face: its body is
background tiles in colours 128-239. NEWLEVELSETUP reads the PALCH table `rocdemfd.txt`
(READITEM 0x62 into `_TorchChange`: 16 steps x 336 bytes), and DEMONWAIT, 500 ticks after the
player lands, rebuilds `_LevelPal` (469b:12c0) from `_VirtPal` with step GenericVar[1] (16 down
to 0) for colours 128-239 at each carry of 0x400, then makes the face hittable and shows the bar.
DEMONFACE slides it to x 0x9c + (player x - `_OrgX`) / 16 and shows frame 1 (the mouth open,
shaking the view) while the roar's sound (GenericVar[2]) plays. Each attack waits (GenericVar[0]
0) and then runs: DEMONSPIT, eight fireballs (type 0x4c, DODRAGONFIRE, xSpeed |dx| * 0xb4) 300
ticks apart; DEMONSUMMON, two fire boars (type 0x51, Boar3ID) from the view's sides, killed after
0x1000 ticks; DEMONSPOUT, two sweeps of 13 drops 0x14 apart from the view's top: spikes (type
0x1c, DemSpikID, DOPROJECTILE kind 6, which on this level lands as a DOCAVEHOLE flame vent) or 1
in `_DiffMode + 3` a potion; DEMONDROP, a boulder (type 0x50) at the view's left or 0x104 right of
it; then DEMONSPIT again. A hit (DemStrID / DemSideID[4]) ends the wait. ENEMYHIT (priority 0x4c)
starts DEMONDIE (GenericVar[3] / [4] the place it was hit, so the bar reads beaten), kills the
boars and sends the spikes off (OFFKILLER).

FDEMONDIE (the ending) decompiles into garbage (Ghidra mis-segments its near jumps), so it was read
from a raw disassembly; its jitter is a 7-case `switch` (table at 2128:104b) on GenericVar[5].
Stages (GenericVar[0]): 0 shaking about the hit point in a random direction (picked at each
carry of 20000, with the view down a pixel and an explosion or a ball of fire, type 0x4c with
GenericVar[0] 1, 9 times in 30); after 4000 ticks GETPALETTE / SETPALETTE fade every component up
a step at each carry of 0x800 to white; 1 AOPEN(0xb4) reads `demgone.bg` over the background
tiles (0xf000 bytes into `_BgBuffer`, 0x6400 into `_Bg2Buffer`, MAPUPDATE redraws); 2 the demon
hidden, the player frozen at (0x132, 0xd7) as MalWalkID[9] whoever plays, and FemWalkID[9] (type
0x24, frozen) at x 0x154; 3 after 500 ticks a fade back down to `_LevelPal`; 4 after 1000 ticks a
light (ChimFireID, hidden) at view (0x8c, 0x3c); 5 sparks (type 0x0e, POWERUPSPARKS: they fly
until they reach the light, or elsewhere the player) from the view's edges at each carry of 6000,
the light showing at 600 ticks (blinking) as ChimFireID[3] and growing to [0] at 1500; 6 after 1000
ticks it sinks (0x640) and drifts right (800) until 10 pixels above the player's head; 7 after
2000 ticks `_quitflag` 3.

**Boulders** (RXPLAT.C:1182 ROLLINGBOULDER, priority 0x97, solid through MAINTAINPLATFORM /
MAINTAINBLOCK). GenericVar[0] 0 dropped, 1 bounced, 2 rolling; GenericVar[2] 0 on the ground, 1
falling (ySpeed + 0xb4 to 32000), 2 rising (GenericVar[4] ticks, then slowing). The first landing
shakes the view (a ROSSSHAKE) and bounces it at -10000; then it rolls a pixel at each carry of
10000, turning backwards through DeadRck1ID[7..0], following slopes, falling off ledges, and
stops at a wall or the map's edge on the ground (STILLBOULDER, Update 0x32). A player riding it is
carried along at 13000. On level 25 it goes once off the screen.

## Scrollers

INITLEVEL's level objects that move the view by themselves (RXPLAT.C). Each pushes a player the
view leaves behind (following the slopes; off a vine) and crushes them against a wall (YOUCRUSH);
the original loops until the player is back in the view, which would hang on a player who can't
move, so the port bounds those loops. Nothing moves while the player is dead or `_LightningFlag`
is set.

- **AUTOSCROLLER** (type 0x29, priority 5; levels 1, 3 and 22 above difficulty 2, and level 14
  after 700 ticks once the player lands if one is placed there): the view scrolls right a pixel
  at each carry of 0x1000 (36 a second) to the map's end, `_WindowXsize` 0x140; the player is
  kept between `_OrgX + 5` and `_OrgX + 0x12f`.
- **BOULDERSPAWNER** (type 0x11, priority 6; levels 17 and 19): four boulders (type 0x50 at y 0x14)
  from the tables at DS:048a (level 17: dropped once `_OrgX` passes the first number) and DS:04a2
  (level 19: once it's below it): (view x, boulder x, xDir). Level 19 is played right to left:
  the view scrolls left a pixel at each carry of 0x1000.
- **CAVESCROLLER** (type 0x2b, level 13, `_CenterOff` -2 so SCREENCENTER leaves it alone): the view
  moves vertically by itself, and its bottom edge (view y 0xc0) is a floor that the player, prizes,
  spikes and debris (priorities 0xe5, 0x29, 0xdc, 0xf0, 0xb9, 0x65) stand on. Flags bit 0 set is
  rising (speeding up by 0x50 a tick to 15000) to `_OrgY` 0; GenericVar[2] counts the trips down:
  the first starts at 7000 and slows by 0x5a until it turns back up, the next waits 200 ticks and
  sinks at 0x1000 to `_OrgY` 100, waits 0x96 and rises again. A step happens when yPart's sum
  carries (or the speed is 0). It carries along torches (DOTORCH, which keep view y 0x81),
  fireballs, whip segments, explosions, gargoyle fire, the player when not on the cave's own
  ground (GenericVar[3] 0), loose spikes and settled blood and prizes. Rising, a player under a
  ceiling is slid out from under it, made to crouch, or crushed. Past x 0x8fc at the top it stops
  (GenericVar[1]).

## Tiles: types and collision

**Collision flags** are bits of the background cells (`MOVE.C`): `CANGOLEFT` fails if a
cell the object's left edge would enter has 0x400, `CANGORIGHT` 0x200, `CANGOUP` 0x1000 and
`CANGODOWN` 0x800 (each checks every cell along that edge, and only when the edge is on a
tile boundary). Bit 0x2000 marks tiles the player can grab (gems, secrets): after each player
move, MOVE.C checks the cells entered and calls `_Grabber` (GRABSTUFF) for those.

**Tile types.** After the object list each `.led` holds three word tables: 512 for the
background tiles (by `BgMap & 0x1ff`), 128 for the foreground tiles and 128 for the bonus
tiles. LOADLEVEL reads them into segment 469b at 17c0, 16c0 and 15c0 (not DGROUP). `CANMOVE` (RXGENERL.C:264) uses the low byte of the type
under the middle of an object's feet: 200-212 are slopes whose surface height per pixel
column is `_slope[k - 200][16]` (DGROUP:00df); it sets `UpOffset` (standing below the
slope's surface) and `DownOffset` (1: can fall, 11: the surface is one pixel below).
GRABSTUFF (RXGRABF.C) reacts to other values (see "Grabbing" above).

**The flag bits, all of them.** 0x200-0x1000 are the four collision bits and 0x2000 the grab bit
above. Bit 14 (0x4000) is set on a few hundred cells of the forest maps only (forest1, forests,
frsteasy) and nothing in ROC.EXE tests it: a scan of every instruction for 0x4000 / `AH, 40h`
finds only other flags (`_GameFlags`, `_OldGameFlags`, file modes). Bit 15 is never set. The 0x600
bytes at the end of each `.led` (after the type tables, never read by LOADLEVEL) have the type
tables' layout, 512 + 128 + 128 words, and hold flag values: 0x1e00 (all four walls), 0x800 (a
floor), 0x2000 (grabbable), 0x600, 0xe00, 0x2600, and in forest1 0x4000. They look like the level
editor's default flags for each tile, which it would stamp into a cell with the tile; the game only
reads the cells' own bits. So no flag work was left for the port beyond what it already did.

## Animated tiles

Water, waterfalls, torches, gems' glints and lava are map cells whose tile is stepped round a cycle
of frames (ANIMTILE.C, assembly at 3b52; no debug info inside).

**Setting up** (NEWLEVELSETUP calls SETUPHANDLES, RXNEWLVF.C:449, after the level's counts and
before RESTOREGAME / INITLEVEL; a CS jump table at 250e:1731 by level). Three cdecl makers, each
`(rate, count, frame, ...)`: `_AnimateBack` (background cells, matched on `& 0x1ff`),
`_AnimateFore` (foreground bytes with bit 7: it ORs 0x80 into its frames) and `_AnimateMask`
(bonus bytes, bit 7 clear). Each `ALLOCNEAR`s a node (kind 0 / 1 / 1 at +0, rate +2, count +4,
counter +6, then 100 cell numbers from +0xa ending at a 0, then the frames at +0xd2), records
every map cell showing any of its frames (a frame listed twice records the cell twice; the 100th
cell is a fatal error), and goes in `_AnimHandle[_NextAnim++]` (32 slots). The background frames
come from `_AnTil[23][3]` (DS:39a0), which INITANTIL (LOADLEV.C:321, table at 38d2:0efa) fills
per level group at the start of LOADLEVEL (600 meaning none) so that TILEBUFFOPTI renumbers them
with the map. NEWLEVEL frees them all (DEANIMATE) when the level ends.

| Levels                     | Animations                                                                  |
|----------------------------|-----------------------------------------------------------------------------|
| 0-2 (forest)               | 7 back x 3 frames, rate 0x23; mask 19/2d/41/55 and 1a/2e/42/56, rate 10      |
| 3, 4, 13, 21 (caves)       | 19 back x 3, rate 0x13; 4 back x 2, rate 0x3c; mask 3c/50/64/78, 3d/51/65/79 |
| 5-7 (castle)               | mask 72-76 (5 frames, rate 0x13); mask 28/3c/50/64, 29/3d/51/65             |
| 8, 9 (swamp)               | mask 0c/20/34/70, 73/74/75/76                                                |
| 11, 12 (mushrooms)         | 2 back x 4, rate 10; mask 3c/50/64/78, 3d/51/65/79                          |
| 14, 15, 22-24              | mask 28/3c/50/64, 29/3d/51/65; 17 back x 3, rate 0x13                       |
| 17-20 (dead lands)         | mask 12/26/3a/4e, 13/27/3b/4f                                               |
| 25 (demon)                 | 6 back x 8 frames, rate 0x13                                                |
| 26 (secret1)               | the caves' two masks; 9 fore x 2 frames (5b-63 / 6f-77) at rates 0xf-0x14  |
| 10, 16, 27, 28             | none                                                                         |

Masks without a rate given run at 10. **Clocking**: the timer task (1000:c5d3) calls 1000:c607
at each of its 582 ticks a second while `_LevelRunning` and `_TicAmt`; at every fourth
(`_AnimCounter`) it adds one to each animation's counter, so a rate is in fours of ticks (0x13: a
step every 76 ticks; 10: every 40). **RUNANIMTILES** (3b52:042f), once a frame between RUNTIMERS
and ANIMATORUPDATE: an animation whose counter reached its rate sets it to 0 and moves each cell
from the frame it shows to the next (the last to the first; flags kept), leaving cells that show
none of them (a gem taken) alone, and MAPUPDATEs the cells inside the view. Not every frame
SETUPHANDLES names is in every map (forest2 uses none of the forest's background ones), which is
the same in the original.

**The port** (`src/animtile.c`): the same nodes, set up by `game_start` after the counts, counted
in `game_frame` and stepped before the objects run; the port redraws the whole view each frame, so
there's no MAPUPDATE. A restored game's maps replace the level's after the setup, as in the
original, and the cells' positions are the same.

## The joystick

ROC.CFG (SETUP.EXE writes it) has `JoystickOn` (byte 46), `MyBUTTON0-3` (bytes 16-19: the
`_KeyTrans` entry each button presses; the shipped file has 4, 5, 6, 7: jump, attack, the special
item, look) and the calibration (bytes 20-31, the JOY struct `_Joy` at DS:1c1c in another order:
Xsense, Xlo, Xhi, Ysense, Ylo, Yhi). JOYCALIBRATE (3c1c:0145, which only SETUP's copy runs) reads
the centred stick and stores centre - sense and centre + sense as lo / hi. READJOY (3c1c:0001)
times the gameport's one-shots on port 201h (up to 5000 loops; JOYDETECT calls it absent at that).
STARTUPCHECKLIST prints "Joystick present / not present / active / not active", turns
`_JoystickOn` off when there's none (which SAVECONFIGFILE then saves), and points `_JoyReader` at
NULLJOYREADER; otherwise `_MachControl` points it at MYJOYREADER. (SYSJOYREADER, the engine's own,
with `_Button0-3pressed`, isn't used.)

**MYJOYREADER** (RXUTILSF.C:30) writes into `_Keys` as the keyboard interrupt does: an x reading
below Xlo holds `_KeyTrans[0]` (left) and lets go of right, above Xhi the reverse, and back inside
it lets go of both, once (DS:53ca; y the same with up / down, DS:53cb). Each button (201h bits
4-7, low when pressed) presses or lets go of `_KeyTrans[_MyBUTTONn]` only when it changes
(DS:53c6-53c9). So a key held on the keyboard is let go when the stick comes back to the middle.
RUNLEVEL calls `_JoyReader(&_Joy)` every frame, and OPENMENU, SELECTMENU, WINDOWCURSOR2, PROMPTWIN,
VOLUMESELECT and ENDWIN call MYJOYREADER in their key loops when `_JoystickOn`. Elsewhere:
PERCENTWIN and FINALPERCENTWIN say "PRESS A BUTTON!" instead of "PRESS ANY KEY!", READERFETCHER
takes `_KeyTrans[_MyBUTTON0]` for Esc and `[_MyBUTTON1]` for Enter, and DISPLAYSCORES leaves on
button 0's key and turns the page on button 1's (with or without a joystick). READBUTTON /
DBREADBUTTON read a button straight (debounced over a tick).

**The port** (`src/host.c`): SDL's first joystick, as a game controller when SDL knows it (the
left stick or the D-pad; A, X, B, Y are buttons 0-3, so with the shipped MyBUTTONs A jumps, X
attacks, B changes hero and Y looks, as an Xbox 360 controller's layout suggests) or as a plain
joystick (axes 0 and 1, hat 0, buttons 0-3), opened again when one is plugged in or out. Beyond
the original (asked for in play testing), the controller has three layouts (`host_pad_mode`): the
game loop's own frames in a level use MyBUTTON0-3; everything else (the menus and windows, the
reader, the title) has A as Enter and B as Esc; the "Press Y" windows (PROMPTWIN, DEATHRESTOREWIN,
ENDWIN) also take the attack button (X) and Y as Y. Start is Esc in all of them, so it opens the
main menu in a level. A button lets go of the key it pressed, whatever the layout is by then. MYJOYREADER runs at every frame (which
covers RUNLEVEL and the windows) while ROC.CFG's `JoystickOn` or `--joystick` is set and one is
plugged in. The threshold is the calibration's margin over its centre as a part of the stick's
throw (half without one). The port doesn't save `JoystickOn` off for a missing joystick. Headless
runs have a scripted joystick (`--joystick` with holds `joyleft`... `button0`...); their key holds
now press and release keys at their ends, as the keyboard's events do, so the joystick can write
into the keys too (no earlier test changes).

## The original's saved games

RXSAVEF.C. **SAVEGAME(FileName, FileDesc)** writes `ROC.SGn`: "ROCX" and a 0, the version 'd', the
20-character description, `_level` (RETRIEVELEVEL reads that byte back; 'd' means none), the
object count (a word), then the maps as they lie in memory: the BGMAP (Rows, Cols, the cells, and
6 bytes past them: Cols x Rows x 2 + 8) and the FGMAP (Cols as a word, the cells), then the
globals in this order: `_OrgX`, `_OrgY`, `_Vine_x`, `_Vine_y`, `_hitagain`, `_MonsterCounter`,
`_MonsterNumber` as a word (with `_FreeHit`), the inventory's bit-field byte (copied through a
stack byte, so bits 6-7 are junk), HitPoints[2], MaxHitPoints[2], `_OldMaxHitPoints[2]`,
WeaponStatus[2], `_LightningFlag`, `_DiffMode`, `_FreeHit`, Score, `_OldScore`, `_NextScoreUp`,
`_OldNextScoreUp`, Gems, `_OldGems`, `_WindowXsize` (word), `_WindowYsize`, `_CenterOff`,
`_HitRelease`, `_WhippingFN / AI / FC / FCM`, `_Looking`, the DAC (GETPALETTE, 768 bytes),
`_GameFlags`, `_OldGameFlags`, `_QuitGame`, `_quitflag`, the level's four "in level" counts, the
found counts each with its total (`_GemFound`, `_GemTotal`...), `_HourTotal / _MinuteTotal /
_SecondTotal`, `_GemTotal` again, `_MinuteInLevel`, `_SecondInLevel`, the four finals,
`_TimerFix` (word), `_TimerTics`, `_NextSecond` and `_ReturnLevel`. Then the objects
(STOREOBJECTINFO), main list, fore list, slow list, then the off-screen list tagged 3 / 4 / 5 by
its home list: list, priority, the Animator's and the OldAnimator's index in the table at
DS:059a (178 near pointers, every animator a saved object can have, ending with OFFKILLER; a
missing one is a fatal error), the index of ImageID in `_Img` (counted as far pointers from
ArrowID: the port's IMG_ numbers), a byte for the global that points at it (1 `_You`, 2 `_Boss`
and `_Neck[0]`, 3 `_HitPts`, 4-7 `_Neck[1-4]`), OBJstruct's Blinks .. Counter (Link, List, ImageID,
Animator and UserPtr are left out) and USERstruct's Friction .. SurfaceFlags (23 bytes; the
OldAnimator and Name pointers are left out). The main and off lists skip DOREFLECTION objects and
objects of priority 0x18 with NULLANIMATOR (the raft's front piece).

**RESTOREGAME** (instead of INITLEVEL, after LOADLEVEL) reads it all back, sets the scroll bounds
and `_YouWalkID`..., and MAKEOBJECT makes each object: Flags | 8, ADDPLINKNODE into its list by
priority (so objects of equal priority come back in reverse order), PARENTLISTADD for what
reflects on levels 8 and 9, and GenericVar[2] = 0 for SWAMPRAFT, which then makes a new front
piece. Near pointers kept in GenericVar (a wisp light's wisp, the raft's front) are restored as
they were and point at nothing in particular; `_Circle1` positions are offsets in its own
segment, so they survive. The palette read is `_SavePal`, which the level fades to.

**Tile numbers.** LOADLEVEL compacts each background layer with TILEBUFFOPTI (LOADLEV.C:230, all
levels but 25 and the title / reader): it marks the tiles the map and `_AnTil` use, moves the
highest used tile into each unused place from the bottom up (renumbering the map's cells and
`_AnTil`), writes the layer to a temporary file (`TILEBUFn`) and reads back only those. So a
saved map numbers its tiles as compacted, not as in the `.bg`. The port doesn't compact; to read
an original save it replays TILEBUFFOPTI on the level as loaded (`animtile_opti_numbers`) and
turns each saved cell's tile back into the `.bg`'s number. Checked on the four saves in the game
folder: before, 4517 of castle1's 7540 cells had other tiles than the level; after, 3 (bricks
the player brought down), and the other levels likewise (forest1: its 7 waterfall cells,
mid-cycle).

An object without an image (the cameras, a hidden wisp) is saved as the index of the first
empty `_Img` slot (STOREOBJECTINFO's search finds a null far pointer there), and MAKEOBJECT reads
`_Img[n]` back as it is, null again; the port does the same (loading that slot's set would give a
camera a sprite).

**The port** (`src/save.c`): each slot is the newer (by the file's time, to the second) of
`ROCP.SGn` and `ROC.SGn`. Saving a slot writes both: `ROC.SGn` first in the original's format
(SAVEGAME / STOREOBJECTINFO, so the DOS game can load it), then `ROCP.SGn` with the port's whole
state, which is then never the older, so the port reloads its own. ROC.SGT (the original's reader
save, deleted once restored) is neither read nor written. The animator table is read from ROC.EXE
at start; an animator missing from it (none is known) fails only the `ROC.SGn`. The tiles are
renumbered with TILEBUFFOPTI's numbering worked out when the level started (taking a gem can
remove a tile's last use, which would shift it). Bits 6-7 of the inventory byte, the 6 bytes past
the BGMAP and an object's OldAnimator when none was set (NULLANIMATOR, as INITOBJ starts them) are
written as the port has them. Unlike the original, the port gives a wisp's lights the nearest wisp
(the pointer it kept is meaningless) and leaves `_quitflag` at 0.

What the original's format can't carry, so a game loaded from it goes on a little differently
from one loaded from `ROCP.SGn`: the random seed, `_DragonMove` (the dragon's necks), the order of
objects of equal priority (ADDPLINKNODE puts them back reversed, so they draw their random numbers
in another order), the raft's front piece until the raft comes into view, and reflections parked
off screen (made again at their parents). Checked: castle1 loaded from the DOS game's ROC.SG0 and
saved at once by the port gives a ROC.SG3 of the same size whose BGMAP is byte for byte the
original's (the renumbering round-trips), with the same 55 objects (lists, priorities, animators,
images); the rest differs only by the ticks played in between. Saving on each of the 27 levels and
loading each format back gives the same player, inventory, counts and objects, but for the
differences above (levels 7, 8, 9, 10 and 26; with the seed forced equal, level 7 is identical).
Not yet tried in the DOS game itself.
