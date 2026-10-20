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
  `value & 0x1ff` into bg layers 1-3 (240 tiles each) plus flag bits 9-14 (not decoded yet);
  foreground byte = 0 or a 7-bit tile index, from `.mfg` if bit 7 is set and `.mbg` if not.
  The foreground map is an `FGMAP`: a u16 `Cols`, then the table (LOADLEVEL reads
  Rows*Cols + 3 bytes, so one spare byte follows). Reading the table from offset 3 instead
  shifts every foreground tile one cell left (tree tops off their trunks, sky showing through
  the gaps), which the port did at first.
- Level *n* uses `name[n].led` and the tile set `name[set[n]]`, where the names come from
  `roclevel.txt` and `set[]` is a 29-byte table at DS:033e (e.g. forest2 uses forest1's tiles).
- Each `.led` ends with 0x600 bytes that `LOADLEVEL` doesn't appear to read (after its
  0x400/0x100/0x100 tables). Not identified yet.

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

Which warp file each castle level reads isn't explicit (READITEM takes an item number), but
castle1 uses types 10-15 and castle2 10-12, matching the 6 and 3 pairs in `.008` / `.009`.

## Damage, attacks and the inventory

**Inventory** (`_YouInventory`, DS:554b, `INVENTORY`): Score (u32), Gems, MaxHitPoints[2],
HitPoints[2], WeaponStatus[2], and a bit-field word: Poisoned1 (bit 0, the male), Poisoned2
(bit 1), WeaponPower1 (bits 2-3, the whip) and WeaponPower2 (bits 4-5, fireballs). The arrays
are indexed by `(_GameFlags & 4) != 0`: 0 the female, 1 the male. NEWLEVELSETUP copies score,
gems and the maximum hit points to `_Old*`, which a death restores. Gems are also the
female's ammunition: each fireball costs one.

**LEVELTIMER** (RXTIMERF.C) is a timer NEWLEVEL installs with a rate of 0x24 ticks (RUNTIMERS
runs it at most once a frame: about 16 times a second). It counts down `_hitagain` (the
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
(priorities 0x3c, 0x47, 0x4a, 0x4c, 0x66) have their own deaths. DOCOLLECTABLE bounces the
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

## Tiles: types and collision

**Collision flags** are bits of the background cells (`MOVE.C`): `CANGOLEFT` fails if a
cell the object's left edge would enter has 0x400, `CANGORIGHT` 0x200, `CANGOUP` 0x1000 and
`CANGODOWN` 0x800 (each checks every cell along that edge, and only when the edge is on a
tile boundary). Bit 0x2000 marks tiles the player can grab (gems, secrets): after each player
move, MOVE.C checks the cells entered and calls `_Grabber` (GRABSTUFF) for those.

**Tile types.** After the object list each `.led` holds three word tables: 512 for the
background tiles (by `BgMap & 0x1ff`), 128 for the foreground tiles and 128 for the bonus
tiles. LOADLEVEL reads them into segment 469b at 17c0, 16c0 and 15c0 (not DGROUP). The 0x600
bytes after them are still unread. `CANMOVE` (RXGENERL.C:264) uses the low byte of the type
under the middle of an object's feet: 200-212 are slopes whose surface height per pixel
column is `_slope[k - 200][16]` (DGROUP:00df); it sets `UpOffset` (standing below the
slope's surface) and `DownOffset` (1: can fall, 11: the surface is one pixel below).
GRABSTUFF (RXGRABF.C) reacts to other values (see "Grabbing" above).

