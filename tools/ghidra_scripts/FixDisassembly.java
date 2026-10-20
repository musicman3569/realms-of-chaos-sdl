// Repair the code Ghidra disassembles wrongly in ROC.EXE: switch tables resolved into the
// wrong segment, and the garbage instructions that follow from them.
//
// Borland C++ compiles a dense switch into
//     CMP BX,n ; JBE ok ; JMP default ; ok: SHL BX,1 ; JMP word ptr CS:[BX + table]
// where the table holds n+1 offsets in the function's own code segment. Ghidra reads the
// table and its targets with the wrong segment base when the function's segment isn't the one
// it used for the memory block (SETUPOBJ's switch at 2a35:001f went to 2000:xxxx). It then
// disassembles garbage in the middle of other code, and that garbage overlaps real
// instructions, so real flow stops there too: SETUPOBJ decompiled to calls to DOSWRITE, and
// GRABSTUFF lost most of its body to a stray target from SETUPHANDLES.
//
// The script, run after ApplyTdInfo.java with the same analysis/ROC.tdinfo.txt:
//  1. finds every such switch in a real function, reads the table at CS:table and sets the
//     case targets as a jump-table override, replacing the old references;
//  2. walks the real code along fall-throughs and jumps, from every source line start (each
//     must begin an instruction) and the case targets, then the named functions, then the
//     unnamed ones; anything overlapping an instruction on that walk is garbage, so it's
//     cleared and the right instruction disassembled (never code the walk already reached,
//     and never the DGROUP globals unless a line start says it's code);
//  3. clears the garbage reached from the old, wrong targets that the walk didn't reach;
//  4. recomputes the function bodies.
// Comments are kept (clearing code would drop them), except "line N" comments that aren't
// on a source line start. Safe to re-run.
//
// Usage: Script Manager -> run FixDisassembly.java and pick analysis/ROC.tdinfo.txt.
// Headless: pass the path as the script argument.
//@category Realms of Chaos

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.PseudoDisassembler;
import ghidra.app.util.PseudoInstruction;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressIterator;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.address.SegmentedAddress;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.CommentType;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.pcode.JumpTable;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.program.model.symbol.SourceType;

public class FixDisassembly extends GhidraScript {

	private Memory mem;
	private Listing listing;
	private PseudoDisassembler pseudo;
	private Set<Address> code = new HashSet<>(); // instructions the walk has reached
	private Set<MemoryBlock> dataBlocks = new HashSet<>(); // where the globals are
	private ReferenceManager refs;

	@Override
	protected void run() throws Exception {
		mem = currentProgram.getMemory();
		listing = currentProgram.getListing();
		refs = currentProgram.getReferenceManager();
		pseudo = new PseudoDisassembler(currentProgram);
		Map<Address, String[]> comments = saveComments();
		String[] args = getScriptArgs();
		File file = args.length > 0 ? new File(args[0])
				: askFile("Turbo Debugger export (analysis/ROC.tdinfo.txt)", "Fix");
		List<Address> lineStarts = lineStarts(file);
		Set<Address> lines = new LinkedHashSet<>(lineStarts);
		int staleLines = dropStaleLineComments(comments, lines);

		// 1. The switches: collect first, fixing one changes the listing being iterated.
		Map<Address, List<Address>> switches = new LinkedHashMap<>();
		Set<Address> stale = new LinkedHashSet<>();
		InstructionIterator it = listing.getInstructions(true);
		List<Address> branches = new ArrayList<>();
		while (it.hasNext() && !monitor.isCancelled()) {
			Instruction ins = it.next();
			Address a = ins.getAddress();
			if (ins.getLength() == 5 && u8(a) == 0x2E && u8(a.add(1)) == 0xFF &&
				u8(a.add(2)) == 0xA7) {
				branches.add(a);
			}
		}
		for (Address at : branches) {
			Function f = getFunctionContaining(at);
			if (!(at instanceof SegmentedAddress sa) || f == null ||
				((SegmentedAddress) f.getEntryPoint()).getSegment() != sa.getSegment()) {
				continue; // not in a real function, or a misaligned copy at another segment
			}
			int cases = caseCount(at);
			if (cases <= 0) {
				continue; // another switch shape (value tables); Ghidra handles those
			}
			int seg = sa.getSegment();
			int table = u16(at.add(3));
			List<Address> targets = new ArrayList<>();
			for (int k = 0; k < cases; k++) {
				Address t = segAddr(seg, u16(segAddr(seg, table + 2 * k)));
				if (!targets.contains(t)) {
					targets.add(t);
				}
			}
			for (Reference r : refs.getReferencesFrom(at)) {
				if (r.getReferenceType().isFlow() || r.getReferenceType().isData()) {
					if (!targets.contains(r.getToAddress())) {
						stale.add(r.getToAddress());
					}
					refs.delete(r);
				}
			}
			switches.put(at, targets);
			println(String.format("%s %s: %d cases, table %04x:%04x", at, f.getName(), cases,
				seg, table));
		}

		// 2. Write the overrides. The targets are disassembled by the walk below.
		List<Address> caseTargets = new ArrayList<>();
		for (Map.Entry<Address, List<Address>> e : switches.entrySet()) {
			Instruction ins = getInstructionAt(e.getKey());
			Function f = getFunctionContaining(e.getKey());
			for (Address t : e.getValue()) {
				ins.addOperandReference(0, t, RefType.COMPUTED_JUMP, SourceType.USER_DEFINED);
				caseTargets.add(t);
			}
			new JumpTable(e.getKey(), new ArrayList<>(e.getValue()), true, 0).writeOverride(f);
		}

		// 3. Walk the real code along fall-throughs and jumps (not calls: Ghidra puts a few
		// near call targets 64 KB away), starting from what is known to be code: source line
		// starts and case targets, then the functions named by the debug info, then the other
		// functions (analysis made some of those from garbage). Whatever overlaps an
		// instruction on the walk is garbage and is cleared, but the walk never clears an
		// instruction it already reached, so a bad starting point can't destroy real code.
		List<Address> named = new ArrayList<>(), unnamed = new ArrayList<>();
		FunctionIterator fit = listing.getFunctions(true);
		while (fit.hasNext()) {
			Function f = fit.next();
			(f.getSymbol().getSource() == SourceType.DEFAULT ? unnamed : named)
				.add(f.getEntryPoint());
		}
		Set<Address> seeds = new LinkedHashSet<>(lineStarts);
		seeds.addAll(caseTargets);
		seeds.addAll(named);
		seeds.addAll(unnamed);
		code = new HashSet<>();
		int repaired = 0, conflicts = 0;
		for (Address seed : seeds) {
			ArrayDeque<Address> work = new ArrayDeque<>();
			work.push(seed);
			while (!work.isEmpty() && !monitor.isCancelled()) {
				Address a = work.pop();
				if (code.contains(a)) {
					continue;
				}
				boolean had = getInstructionAt(a) != null;
				boolean known = a.equals(seed) && (lines.contains(a) || caseTargets.contains(a));
				if (!disassembleAt(a, known)) {
					conflicts++;
					continue;
				}
				if (!had) {
					repaired++;
				}
				code.add(a);
				Instruction ins = getInstructionAt(a);
				if (ins.getFallThrough() != null) {
					work.push(ins.getFallThrough());
				}
				if (ins.getFlowType().isJump()) {
					for (Address t : ins.getFlows()) {
						work.push(nearTarget(a, t));
					}
				}
			}
		}

		// Garbage left from the old switch targets: the instructions reachable from them
		// that the walk didn't reach.
		int cleared = 0;
		for (Address t : stale) {
			ArrayDeque<Address> junk = new ArrayDeque<>();
			junk.push(t);
			Set<Address> seen = new HashSet<>();
			while (!junk.isEmpty()) {
				Address a = junk.pop();
				Instruction ins = getInstructionAt(a);
				if (ins == null || code.contains(a) || seeds.contains(a) || lines.contains(a) ||
					!seen.add(a)) {
					continue;
				}
				if (ins.getFallThrough() != null) {
					junk.push(ins.getFallThrough());
				}
				if (ins.getFlowType().isJump()) {
					for (Address j : ins.getFlows()) {
						junk.push(j);
					}
				}
			}
			for (Address a : seen) {
				listing.clearCodeUnits(a, a, false);
				cleared++;
			}
		}

		// 4. Function bodies.
		fit = listing.getFunctions(true);
		while (fit.hasNext() && !monitor.isCancelled()) {
			CreateFunctionCmd.fixupFunctionBody(currentProgram, fit.next(), monitor);
		}
		restoreComments(comments);

		int missing = 0;
		for (Address a : lineStarts) {
			if (getInstructionAt(a) == null) {
				missing++;
			}
		}
		println(String.format("FixDisassembly: %d switches fixed, %d instructions disassembled, " +
			"%d garbage instructions cleared, %d flow targets that aren't code, " +
			"%d of %d line starts still not code, %d misplaced line comments removed",
			switches.size(), repaired, cleared, conflicts, missing, lineStarts.size(),
			staleLines));
	}

	/** n+1 for "CMP BX,n ; JBE +3 ; JMP rel16 ; SHL BX,1" just before the indirect jump. */
	private int caseCount(Address jmp) throws Exception {
		Address shl = jmp.subtract(2), jmpDef = shl.subtract(3), jbe = jmpDef.subtract(2);
		if (u8(shl) != 0xD1 || u8(shl.add(1)) != 0xE3 || u8(jmpDef) != 0xE9 ||
			u8(jbe) != 0x76 || u8(jbe.add(1)) != 0x03) {
			return -1;
		}
		Address c8 = jbe.subtract(3), c16 = jbe.subtract(4);
		if (u8(c8) == 0x83 && u8(c8.add(1)) == 0xFB) {
			return u8(c8.add(2)) + 1;
		}
		if (u8(c16) == 0x81 && u8(c16.add(1)) == 0xFB) {
			return u16(c16.add(2)) + 1;
		}
		return -1;
	}

	/** A jump target in the jump's own segment: Ghidra doesn't wrap the 16-bit offset, so a
	 * near jump or call that wraps lands 64 KB away (in DGROUP, for some). */
	private Address nearTarget(Address from, Address to) {
		if (!(from instanceof SegmentedAddress sa)) {
			return to;
		}
		long off = to.getOffset() - sa.getSegment() * 16L;
		return off >= 0 && off < 0x10000 ? to : segAddr(sa.getSegment(), (int) off);
	}

	/** Disassemble at an instruction start, clearing whatever misaligned code or data
	 * overlaps the instruction there. Data in the blocks that hold the debug info's globals
	 * is only cleared if the address is known to be code (a source line start or a case
	 * target); data analysis made inside code segments is cleared on the walk too. */
	private boolean disassembleAt(Address t, boolean known) throws Exception {
		if (getInstructionAt(t) != null) {
			return true;
		}
		PseudoInstruction pi;
		try {
			pi = pseudo.disassemble(t);
		}
		catch (Exception e) {
			pi = null;
		}
		if (pi == null) {
			return false; // not code (a bad flow target, data...)
		}
		Address start = t, end = t.add(pi.getLength() - 1);
		CodeUnit first = listing.getCodeUnitContaining(start);
		CodeUnit last = listing.getCodeUnitContaining(end);
		if (first != null) {
			start = first.getMinAddress();
		}
		if (last != null) {
			end = last.getMaxAddress();
		}
		for (CodeUnit cu : listing.getCodeUnits(new AddressSet(start, end), true)) {
			if (code.contains(cu.getAddress())) {
				return false; // overlaps code the walk vouched for: t is the bad one
			}
			if (!known && cu instanceof Data d && d.isDefined() &&
				dataBlocks.contains(mem.getBlock(cu.getAddress()))) {
				return false; // a global in DGROUP: a bad flow target, not code
			}
		}
		listing.clearCodeUnits(start, end, false);
		DisassembleCommand cmd = new DisassembleCommand(t, null, true);
		cmd.applyTo(currentProgram, monitor);
		return getInstructionAt(t) != null;
	}

	/** The source line starts (LINE rows with code) from the tdinfo.py export; also notes
	 * the memory blocks that hold its globals (DATA rows). */
	private List<Address> lineStarts(File file) throws Exception {
		List<Address> out = new ArrayList<>();
		try (BufferedReader r = new BufferedReader(new FileReader(file))) {
			for (String line; (line = r.readLine()) != null;) {
				String[] f = line.split("	", -1);
				if (f[0].equals("LINE") && Long.parseLong(f[2]) > 0) {
					out.add(toAddr(f[1]));
				}
				else if (f[0].equals("DATA")) {
					MemoryBlock b = mem.getBlock(toAddr(f[1]));
					if (b != null) {
						dataBlocks.add(b);
					}
				}
			}
		}
		out.sort(null);
		return out;
	}

	/** Remove "line N" comments that aren't on a line start (an earlier ApplyTdInfo run put
	 * some a byte early). ApplyTdInfo writes the right ones. */
	private int dropStaleLineComments(Map<Address, String[]> comments, Set<Address> lines) {
		int n = 0;
		for (Map.Entry<Address, String[]> e : comments.entrySet()) {
			String c = e.getValue()[CommentType.EOL.ordinal()];
			if (c != null && c.startsWith("line ") && !lines.contains(e.getKey())) {
				listing.setComment(e.getKey(), CommentType.EOL, null);
				e.getValue()[CommentType.EOL.ordinal()] = null;
				n++;
			}
		}
		return n;
	}

	private Map<Address, String[]> saveComments() {
		Map<Address, String[]> saved = new HashMap<>();
		AddressIterator ai = listing.getCommentAddressIterator(mem, true);
		while (ai.hasNext()) {
			Address a = ai.next();
			String[] c = new String[CommentType.values().length];
			for (CommentType type : CommentType.values()) {
				c[type.ordinal()] = listing.getComment(type, a);
			}
			saved.put(a, c);
		}
		return saved;
	}

	private void restoreComments(Map<Address, String[]> saved) {
		for (Map.Entry<Address, String[]> e : saved.entrySet()) {
			for (CommentType type : CommentType.values()) {
				String c = e.getValue()[type.ordinal()];
				if (c != null && listing.getComment(type, e.getKey()) == null) {
					listing.setComment(e.getKey(), type, c);
				}
			}
		}
	}

	private Address segAddr(int seg, int off) {
		return toAddr(String.format("%04x:%04x", seg, off & 0xFFFF));
	}

	private int u8(Address a) throws Exception {
		return mem.getByte(a) & 0xFF;
	}

	private int u16(Address a) throws Exception {
		return u8(a) | u8(a.add(1)) << 8;
	}
}
