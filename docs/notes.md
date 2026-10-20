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
at the wrong addresses in Ghidra.

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
  Gaps in the scenery use sky/horizon tiles because the parallax backdrop shows through
  them in the game.
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
