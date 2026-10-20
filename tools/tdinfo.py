#!/usr/bin/env python3
"""Parse the Turbo Debugger symbol table appended to a Borland C++ EXE and export it as text.

Usage: python tdinfo.py ROC.EXE OUTPUT.txt [--base 1000]

ROC.EXE was linked with debug info (TLINK /v), so the 246 KB after the load image is a Turbo
Debugger 3.x symbol table: public and static names, source files, line numbers, C types and
the parameters/locals of every function. Ghidra's MZ loader ignores it. This script writes it
out as tab-separated lines that tools/ghidra_scripts/ApplyTdInfo.java applies to the Ghidra
program. Read-only: it only reads the EXE.

Layout (version 0x0310, all little-endian), each table follows the previous one:
  header (0x32 bytes) + extension
  symbols      9 bytes: name, type, offset, segment, class
  modules     16 bytes: name, language, model, symbol idx/count, file idx/count, corr idx/count
  source files 6 bytes: name, DOS timestamp
  line numbers 4 bytes: line, offset in the code segment
  scopes      12 bytes: symbol idx/count, parent scope, ?, offset, length
  segments    16 bytes: module, code segment, offset, length, scope idx/count, corr idx/count
  correlations 8 bytes: segment, file, line idx/count
  types        8 bytes: kind, name, size, 3 extra bytes (extra[0] flags, extra[1:3] an index)
  members      5 bytes: flags, name, type
  names pool: NUL-terminated strings at the very end of the file
All indices are 1-based. Segments are load-image relative (add the load segment, 1000 in
Ghidra). The first header.globals symbols are the public symbols, sorted by name.

Symbol class: low 3 bits 0=static 1=absolute 2=auto 4=register 5=const 6=typedef 7=tag;
bit 3 (0x08) marks a parameter; 24 (0x18) is a C function. Register numbers: 0 AX, 1 CX,
2 DX, 3 BX, 4 SP, 5 BP, 6 SI, 7 DI.

Type kinds used here: 04 char, 05 int, 06 long, 08 uchar, 09 uint, 0a ulong, 0d float,
0f double, 10 long double, 15 near pointer and 16 far pointer (extra index = target),
1a array (size = total bytes, extra index = element), 1e struct and 1f union (extra index =
first member), 22 enum, 23 function (extra[0]: bit 0 pascal, bit 2 far, bit 7 varargs?;
extra index = return type). Integer types are followed by a slot holding their min/max range.
Members are laid out in order with no padding (Borland's default byte alignment); member
flags 1-63 are a bitfield width, 0x40 ends the list, 0x80 marks a non-field record.

Output lines (addresses are Ghidra seg:off; TYPE is a type expression, see below):
  COMPOSITE  struct|union name size
  FIELD      composite offset name TYPE bit_offset bits     (bits 0 = not a bitfield)
  TYPEDEF    name TYPE
  FUNC       addr name module file line conv far varargs RETURN-TYPE
  CODE       addr name module                               public without debug info
  DATA       addr name module TYPE                          TYPE may be empty
  LINE       addr length file line
  VAR        func kind name where TYPE   kind param/local/reg; where = BP offset or register
A type expression is a base type (void char uchar int uint long ulong float double ldouble
bool undefinedN, or a composite name) followed by space-separated modifiers applied left to
right: "*" near pointer, "far*" far pointer, "[N]" array. E.g. "OBJstruct *", "char far* [4]".
"""

import argparse
import struct
from dataclasses import dataclass

MAGIC = 0x52FB
REGS = ["AX", "CX", "DX", "BX", "SP", "BP", "SI", "DI"]
BASIC = {0x00: "void", 0x04: "char", 0x05: "int", 0x06: "long", 0x08: "uchar", 0x09: "uint",
         0x0A: "ulong", 0x0C: "char", 0x0D: "float", 0x0F: "double", 0x10: "ldouble",
         0x28: "bool"}


@dataclass
class Symbol:
    name: str
    type: int
    offset: int
    segment: int
    cls: int


@dataclass
class Module:
    name: str
    language: int
    model: int
    sym_index: int
    sym_count: int
    file_index: int
    file_count: int
    corr_index: int
    corr_count: int


@dataclass
class Scope:
    sym_index: int
    sym_count: int
    parent: int
    unknown: int
    offset: int
    length: int


@dataclass
class Segment:
    module: int
    code_segment: int
    code_offset: int
    code_length: int
    scope_index: int
    scope_count: int
    corr_index: int
    corr_count: int


@dataclass
class Type:
    kind: int
    name: str
    size: int
    flags: int
    index: int


@dataclass
class Member:
    flags: int
    name: str
    type: int


class TdInfo:
    def __init__(self, exe):
        hdr = struct.unpack_from("<14H", exe, 0)
        image_end = hdr[2] * 512 - ((512 - hdr[1]) if hdr[1] else 0)
        t = exe[image_end:]
        (magic, self.version, pool_size, n_names, n_types, n_members, n_syms,
         self.n_globals, n_mods, n_locals, n_scopes, n_lines, n_files, n_segs, n_corrs,
         self.image_size) = struct.unpack_from("<HHI12HI", t, 0)
        if magic != MAGIC:
            raise ValueError(f"no Turbo Debugger info after the image (found {magic:04x})")
        ext_size = struct.unpack_from("<H", t, 0x2E)[0]

        pool = t[len(t) - pool_size:].split(b"\0")[:n_names]
        names = [""] + [n.decode("latin-1") for n in pool]

        def name(i):
            return names[i] if i < len(names) else ""

        pos = 0x30 + ext_size

        def table(fmt, count, make):
            nonlocal pos
            size = struct.calcsize(fmt)
            rows = [None] + [make(*struct.unpack_from(fmt, t, pos + i * size))
                             for i in range(count)]
            pos += size * count
            return rows

        self.symbols = table("<HHHHB", n_syms, lambda n, *r: Symbol(names[n], *r))
        self.modules = table("<HBBHHHHHH", n_mods, lambda n, *r: Module(names[n], *r))
        self.files = table("<HI", n_files, lambda n, ts: names[n])
        self.lines = table("<HH", n_lines, lambda line, off: (line, off))
        self.scopes = table("<6H", n_scopes, Scope)
        self.segments = table("<8H", n_segs, Segment)
        self.corrs = table("<4H", n_corrs, lambda *r: r)
        self.types = table("<BHHBH", n_types, lambda k, n, *r: Type(k, name(n), *r))
        self.members = table("<BHH", n_members, lambda f, n, ty: Member(f, name(n), ty))
        if n_locals:
            raise ValueError("locals table present; layout not handled")

        self.dgroup = next((s.segment for s in self.symbols[1:self.n_globals + 1]
                            if s.name == "DATASEG@"), None)


class TypeNamer:
    """Turns type indices into type expressions and collects the composites they use."""

    def __init__(self, td):
        self.td = td
        self.typedef_name = {}  # composite index -> first typedef naming it
        for s in td.symbols[1:]:
            if s.cls & 7 == 6 and 0 < s.type < len(td.types):
                self.typedef_name.setdefault(s.type, s.name)
        self.comp_name = {}     # type index -> exported composite name
        self.by_name = {}       # composite name -> layout signature
        self.layouts = []       # (kind, name, size, fields) in definition order
        self.pending = []

    def get(self, i):
        return self.td.types[i] if 0 < i < len(self.td.types) else None

    def size(self, i):
        t = self.get(i)
        return t.size if t else 0

    def expr(self, i, depth=0):
        t = self.get(i)
        if t is None or depth > 16:
            return "void"
        if t.kind in BASIC:
            return BASIC[t.kind]
        if t.kind in (0x15, 0x16):
            target = self.get(t.index)
            base = "void" if target is None or target.kind == 0x23 else self.expr(t.index, depth + 1)
            return base + (" *" if t.kind == 0x15 else " far*")
        if t.kind == 0x1A:
            esize = self.size(t.index)
            count = t.size // esize if esize else 0
            if count <= 0:
                return f"undefined{t.size}" if t.size else "void"
            return f"{self.expr(t.index, depth + 1)} [{count}]"
        if t.kind in (0x1E, 0x1F):
            return self.composite(i)
        if t.kind == 0x22:
            return {1: "uchar", 2: "uint", 4: "ulong"}.get(t.size, f"undefined{t.size}")
        return f"undefined{t.size}" if t.size else "void"

    def fields(self, i):
        """Yield (offset, name, type index, bit offset, bits) for a struct/union type."""
        t = self.td.types[i]
        off = bitpos = 0
        k = t.index
        while 0 < k < len(self.td.members):
            m = self.td.members[k]
            k += 1
            if m.flags & 0x80:
                if m.flags & 0x40:
                    break
                continue
            bits = m.flags & 0x3F
            if bits:
                yield (off + bitpos // 8, m.name, m.type, bitpos % 8, bits)
                bitpos += bits
            else:
                if bitpos:
                    off += (bitpos + 7) // 8
                    bitpos = 0
                yield (0 if t.kind == 0x1F else off, m.name, m.type, 0, 0)
                if t.kind == 0x1E:
                    off += self.size(m.type)
            if m.flags & 0x40:
                break

    def composite(self, i):
        if i in self.comp_name:
            return self.comp_name[i]
        t = self.td.types[i]
        name = t.name or self.typedef_name.get(i) or f"anon_{i}"
        sig = (t.kind, t.size, tuple((o, n, self.size(ty), bo, b)
                                     for o, n, ty, bo, b in self.fields(i)))
        if name in self.by_name and self.by_name[name] != sig:
            name = f"{name}_{i}"
        self.comp_name[i] = name
        if name not in self.by_name:
            self.by_name[name] = sig
            self.pending.append((i, name))
        return name

    def flush(self):
        """Define every composite referenced so far (and the ones their fields reference)."""
        while self.pending:
            i, name = self.pending.pop(0)
            t = self.td.types[i]
            fields = [(o, n, self.expr(ty), bo, b) for o, n, ty, bo, b in self.fields(i)]
            self.layouts.append(("union" if t.kind == 0x1F else "struct", name, t.size, fields))


def export(td, base, out):
    def addr(seg, off):
        return f"{seg + base:04x}:{off:04x}"

    def short(path):
        return path.replace("\\", "/").rsplit("/", 1)[-1]

    types = TypeNamer(td)
    body = []

    def emit(*cols):
        body.append("\t".join(str(c) for c in cols))

    # Code ranges -> module, and top-level scope starts -> function.
    seg_module = []
    funcs = {}  # (seg, off) -> (scope index, module)
    for seg in td.segments[1:]:
        mod = td.modules[seg.module].name
        seg_module.append((seg.code_segment, seg.code_offset,
                           seg.code_offset + seg.code_length, mod))
        for i in range(seg.scope_index, seg.scope_index + seg.scope_count):
            if td.scopes[i].parent == 0:
                funcs[(seg.code_segment, td.scopes[i].offset)] = (i, mod)

    def module_at(seg, off):
        for s, start, end, mod in seg_module:
            if s == seg and start <= off < end:
                return mod
        return ""

    # Named addresses: publics first, then statics from each module's symbol list.
    named = {}
    for i in range(1, td.n_globals + 1):
        s = td.symbols[i]
        if s.cls & 7 == 0:
            named.setdefault((s.segment, s.offset), s)
    for m in td.modules[1:]:
        for i in range(m.sym_index, m.sym_index + m.sym_count):
            s = td.symbols[i]
            if s.cls & 7 == 0 and (s.segment or s.offset):
                named.setdefault((s.segment, s.offset), s)

    # First source line of each function, and the line -> code range map.
    first_line = {}
    line_rows = []
    for seg in td.segments[1:]:
        for c in range(seg.corr_index, seg.corr_index + seg.corr_count):
            _, file_idx, line_idx, line_count = td.corrs[c]
            fname = short(td.files[file_idx])
            rows = sorted(td.lines[line_idx:line_idx + line_count], key=lambda r: (r[1], r[0]))
            offsets = sorted({off for _, off in rows})
            end = {o: (offsets[k + 1] if k + 1 < len(offsets)
                       else seg.code_offset + seg.code_length)
                   for k, o in enumerate(offsets)}
            for line, off in rows:
                line_rows.append((seg.code_segment, off, end[off] - off, fname, line))
                if (seg.code_segment, off) in funcs:
                    first_line.setdefault((seg.code_segment, off), (fname, line))

    counts = {"FUNC": 0, "CODE": 0, "DATA": 0, "LINE": 0, "VAR": 0, "TYPEDEF": 0}
    for (seg, off), (scope_idx, mod) in sorted(funcs.items()):
        sym = named.get((seg, off))
        name = sym.name if sym else f"{mod.lower()}_static_{seg + base:04x}_{off:04x}"
        fname, line = first_line.get((seg, off), ("", 0))
        ftype = types.get(sym.type) if sym else None
        if ftype is not None and ftype.kind == 0x23:
            conv = "pascal" if ftype.flags & 1 else "cdecl"
            dist = "far" if ftype.flags & 4 else "near"
            varargs = 1 if ftype.flags & 0x80 else 0
            ret = types.expr(ftype.index)
        else:
            conv, dist, varargs, ret = "", "", 0, ""
        emit("FUNC", addr(seg, off), name, mod, fname, line, conv, dist, varargs, ret)
        counts["FUNC"] += 1
    for (seg, off), sym in sorted(named.items()):
        if (seg, off) in funcs:
            continue
        if seg == td.dgroup:
            emit("DATA", addr(seg, off), sym.name, module_at(seg, off),
                 types.expr(sym.type) if sym.type else "")
            counts["DATA"] += 1
        else:
            emit("CODE", addr(seg, off), sym.name, module_at(seg, off))
            counts["CODE"] += 1
    for seg, off, length, fname, line in sorted(line_rows):
        emit("LINE", addr(seg, off), length, fname, line)
        counts["LINE"] += 1

    # Variables: walk each function's scope and its nested block scopes, in symbol order
    # (declaration order, so the params come out in source order).
    for seg in td.segments[1:]:
        for i in range(seg.scope_index, seg.scope_index + seg.scope_count):
            top = i
            while td.scopes[top].parent:
                top = td.scopes[top].parent
            func = addr(seg.code_segment, td.scopes[top].offset)
            sc = td.scopes[i]
            for k in range(sc.sym_index, sc.sym_index + sc.sym_count):
                s = td.symbols[k]
                c = s.cls & 7
                if c == 2:
                    kind = "param" if s.cls & 8 else "local"
                    bp = s.offset - 0x10000 if s.offset >= 0x8000 else s.offset
                    where = f"{bp:+d}"
                elif c == 4:
                    kind = "reg"
                    where = REGS[s.offset] if s.offset < len(REGS) else f"r{s.offset}"
                else:
                    continue
                emit("VAR", func, kind, s.name, where, types.expr(s.type))
                counts["VAR"] += 1

    # Typedefs (first definition of each name wins).
    seen = set()
    for s in td.symbols[1:]:
        if s.cls & 7 == 6 and s.name not in seen and 0 < s.type < len(td.types):
            seen.add(s.name)
            e = types.expr(s.type)
            if e != s.name:
                emit("TYPEDEF", s.name, e)
                counts["TYPEDEF"] += 1

    types.flush()
    out.write(f"# Generated by tools/tdinfo.py (Turbo Debugger info v{td.version:04x}); "
              f"load segment {base:04x}\n")
    for kind, name, size, fields in types.layouts:
        out.write(f"COMPOSITE\t{kind}\t{name}\t{size}\n")
    for kind, name, size, fields in types.layouts:
        for o, n, e, bo, b in fields:
            out.write(f"FIELD\t{name}\t{o}\t{n}\t{e}\t{bo}\t{b}\n")
    for row in body:
        out.write(row + "\n")
    counts["COMPOSITE"] = len(types.layouts)
    return counts


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("exe")
    ap.add_argument("output")
    ap.add_argument("--base", default="1000", help="load segment used by Ghidra (hex)")
    args = ap.parse_args()

    with open(args.exe, "rb") as f:
        td = TdInfo(f.read())
    with open(args.output, "w", encoding="utf-8", newline="\n") as out:
        counts = export(td, int(args.base, 16), out)
    print(f"{len(td.modules) - 1} modules, {len(td.files) - 1} source files, "
          f"{len(td.symbols) - 1} symbols ({td.n_globals} public), {len(td.types) - 1} types")
    print(", ".join(f"{v} {k}" for k, v in counts.items()), "->", args.output)


if __name__ == "__main__":
    main()
