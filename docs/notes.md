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

Source modules (from the line-number table): the game is `RX*.C` plus `rocxms.asm`,
`rocport.asm` and `rocfile.asm`; the engine is `SPRITE.C`, `MOVE.C`, `ANIMTILE.C`,
`LOADLEV.C`, `KBD3.C`, `TIMER.C`, `XMS.C`, `EMS.C` and the `DRW*.ASM` blitters;
`HUF.C`/`DECODE.C`/`MAKETBL.C` look like the decompressor (probably for `ROC.DAT`); the sound
library is `SOURCE\*.C` (`BLASTER.C`, `AL_MIDI.C`, `MULTIVOC.C`, `FX_MAN.C`, `TASK_MAN.C`…),
which looks like the Apogee Sound System.
