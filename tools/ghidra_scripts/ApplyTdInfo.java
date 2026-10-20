// Apply the Turbo Debugger symbols exported by tools/tdinfo.py to the current program.
//
// Defines the program's structs/unions/typedefs (category /TurboDebugger), creates and names
// functions, gives every function with debug info its real prototype (pascal or cdecl, near
// or far, typed parameters at their stack offsets) and typed locals, labels and types global
// data, maps source lines (Ghidra's source map plus a "line N" EOL comment) and writes a plate
// comment with each function's source location and variables. Safe to re-run.
//
// Usage: Script Manager -> add realms-of-chaos-port/tools/ghidra_scripts as a script
// directory -> run ApplyTdInfo.java and pick analysis/ROC.tdinfo.txt. Headless: pass the
// path as the script argument.
//@category Realms of Chaos

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.database.sourcemap.SourceFile;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.data.*;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Function.FunctionUpdateType;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.listing.ParameterImpl;
import ghidra.program.model.listing.ReturnParameterImpl;
import ghidra.program.model.listing.StackFrame;
import ghidra.program.model.listing.Variable;
import ghidra.program.model.listing.VariableStorage;
import ghidra.program.model.sourcemap.SourceFileManager;
import ghidra.program.model.sourcemap.SourceMapEntry;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;

public class ApplyTdInfo extends GhidraScript {

	private static final CategoryPath CATEGORY = new CategoryPath("/TurboDebugger");

	private DataTypeManager dtm;
	private final Map<String, DataType> named = new HashMap<>();
	private int types, created, renamed, labeled, typedData, prototypes, locals, lines, failed;

	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		File file = args.length > 0 ? new File(args[0])
				: askFile("Turbo Debugger export (analysis/ROC.tdinfo.txt)", "Apply");

		List<String[]> rows = new ArrayList<>();
		try (BufferedReader r = new BufferedReader(new FileReader(file))) {
			for (String line; (line = r.readLine()) != null;) {
				if (!line.isEmpty() && !line.startsWith("#")) {
					rows.add(line.split("\t", -1));
				}
			}
		}
		monitor.initialize(rows.size());
		dtm = currentProgram.getDataTypeManager();

		// Types first: empty composites (so fields can point at each other), fields, typedefs.
		for (String[] f : rows) {
			if (f[0].equals("COMPOSITE")) {
				defineComposite(f[1], f[2], Integer.parseInt(f[3]));
			}
		}
		for (String[] f : rows) {
			if (f[0].equals("FIELD")) {
				addField(f);
			}
			else if (f[0].equals("TYPEDEF")) {
				DataType dt = parseType(f[2]);
				if (dt != null && !(dt instanceof VoidDataType)) {
					named.put(f[1], dtm.addDataType(new TypedefDataType(CATEGORY, f[1], dt, dtm),
						DataTypeConflictHandler.REPLACE_HANDLER));
					types++;
				}
			}
		}

		// Variables grouped by function, for prototypes, locals and plate comments.
		Map<String, List<String[]>> vars = new HashMap<>();
		for (String[] f : rows) {
			if (f[0].equals("VAR")) {
				vars.computeIfAbsent(f[1], k -> new ArrayList<>()).add(f);
			}
		}

		removeDataSegmentFunctions(rows);

		SourceFileManager sfm = currentProgram.getSourceFileManager();
		Map<String, SourceFile> sourceFiles = new LinkedHashMap<>();
		Map<String, StringBuilder> lineComments = new LinkedHashMap<>();

		for (String[] f : rows) {
			monitor.checkCancelled();
			monitor.incrementProgress(1);
			switch (f[0]) {
				case "FUNC" -> applyFunction(toAddr(f[1]), f, vars.get(f[1]));
				case "CODE" -> applyCode(toAddr(f[1]), f[2]);
				case "DATA" -> applyData(toAddr(f[1]), f[2], f[4]);
				case "LINE" -> {
					applyLine(sfm, sourceFiles, toAddr(f[1]), f);
					StringBuilder c = lineComments.get(f[1]);
					if (c == null) {
						lineComments.put(f[1], new StringBuilder("line ").append(f[4]));
					}
					else {
						c.append(", ").append(f[4]);
					}
				}
				default -> { }
			}
		}

		// "line N" EOL comments; only replace comments this script wrote.
		for (Map.Entry<String, StringBuilder> e : lineComments.entrySet()) {
			Address addr = toAddr(e.getKey());
			String old = getEOLComment(addr);
			if (old == null || old.startsWith("line ")) {
				setEOLComment(addr, e.getValue().toString());
			}
		}
		println(String.format(
			"ApplyTdInfo: %d types, %d functions created, %d renamed, %d prototypes, " +
				"%d locals, %d labels, %d data typed, %d source lines, %d failures",
			types, created, renamed, prototypes, locals, labeled, typedData, lines, failed));
	}

	// ---- types ----

	private void defineComposite(String kind, String name, int size) {
		DataType existing = dtm.getDataType(CATEGORY, name);
		Composite c;
		if (kind.equals("union")) {
			if (existing instanceof Union u) {
				while (u.getNumComponents() > 0) {
					u.delete(0);
				}
				c = u;
			}
			else {
				c = (Composite) dtm.addDataType(new UnionDataType(CATEGORY, name, dtm),
					DataTypeConflictHandler.REPLACE_HANDLER);
			}
		}
		else {
			if (existing instanceof Structure s) {
				s.deleteAll();
				if (s.getLength() < size) {
					s.growStructure(size - s.getLength());
				}
				c = s;
			}
			else {
				c = (Composite) dtm.addDataType(new StructureDataType(CATEGORY, name, size, dtm),
					DataTypeConflictHandler.REPLACE_HANDLER);
			}
		}
		named.put(name, c);
		types++;
	}

	private void addField(String[] f) {
		DataType owner = named.get(f[1]);
		int offset = Integer.parseInt(f[2]);
		DataType dt = parseType(f[4]);
		int bitOffset = Integer.parseInt(f[5]), bits = Integer.parseInt(f[6]);
		if (dt == null || dt.getLength() <= 0) {
			failed++;
			return;
		}
		try {
			if (owner instanceof Union u) {
				u.add(dt, f[3], null);
			}
			else if (owner instanceof Structure s) {
				if (bits > 0) {
					int bytes = (bitOffset + bits + 7) / 8;
					s.insertBitFieldAt(offset, bytes, bitOffset, dt, bits, f[3], null);
				}
				else {
					if (offset + dt.getLength() > s.getLength()) {
						s.growStructure(offset + dt.getLength() - s.getLength());
					}
					s.replaceAtOffset(offset, dt, dt.getLength(), f[3], null);
				}
			}
		}
		catch (Exception e) {
			printerr("field " + f[1] + "." + f[3] + ": " + e.getMessage());
			failed++;
		}
	}

	/** Parse a tdinfo.py type expression: base type, then "*", "far*" or "[N]" modifiers. */
	private DataType parseType(String expr) {
		if (expr == null || expr.isEmpty()) {
			return null;
		}
		String[] tok = expr.split(" ");
		DataType dt = switch (tok[0]) {
			case "void" -> VoidDataType.dataType;
			case "char" -> CharDataType.dataType;
			case "uchar" -> UnsignedCharDataType.dataType;
			case "int" -> IntegerDataType.dataType;
			case "uint" -> UnsignedIntegerDataType.dataType;
			case "long" -> LongDataType.dataType;
			case "ulong" -> UnsignedLongDataType.dataType;
			case "float" -> FloatDataType.dataType;
			case "double" -> DoubleDataType.dataType;
			case "ldouble" -> LongDoubleDataType.dataType;
			case "bool" -> BooleanDataType.dataType;
			default -> tok[0].startsWith("undefined")
					? Undefined.getUndefinedDataType(Integer.parseInt(tok[0].substring(9)))
					: named.get(tok[0]);
		};
		// Built-in types are sized by Ghidra's default data organization (int = 4 bytes)
		// until they are attached to the program, whose x86-16 organization has int = 2.
		if (dt != null) {
			dt = dt.clone(dtm);
		}
		for (int i = 1; i < tok.length && dt != null; i++) {
			String m = tok[i];
			if (m.equals("*")) {
				dt = new PointerDataType(dt, 2, dtm);
			}
			else if (m.equals("far*")) {
				dt = new PointerDataType(dt, 4, dtm);
			}
			else if (m.startsWith("[")) {
				int n = Integer.parseInt(m.substring(1, m.length() - 1));
				dt = dt.getLength() > 0 ? new ArrayDataType(dt, n, dt.getLength(), dtm) : null;
			}
		}
		return dt == null ? null : dtm.resolve(dt, DataTypeConflictHandler.DEFAULT_HANDLER);
	}

	// ---- functions ----

	private Function makeFunction(Address addr, String name) throws Exception {
		if (getInstructionAt(addr) == null) {
			clearListing(addr);
			new DisassembleCommand(addr, null, true).applyTo(currentProgram, monitor);
		}
		Function fn = createFunction(addr, name);
		if (fn != null) {
			created++;
		}
		return fn;
	}

	private void applyFunction(Address addr, String[] f, List<String[]> fvars) throws Exception {
		Function fn = getFunctionAt(addr);
		if (fn == null) {
			fn = makeFunction(addr, f[2]);
			if (fn == null) {
				printerr("could not create function " + f[2] + " at " + addr);
				failed++;
				return;
			}
		}
		else if (!fn.getName().equals(f[2])) {
			fn.setName(f[2], SourceType.IMPORTED);
			renamed++;
		}
		if (fvars == null) {
			fvars = List.of();
		}

		StringBuilder plate = new StringBuilder();
		plate.append(f[4]).append(':').append(f[5]).append("  (module ").append(f[3]).append(')');
		if (!f[6].isEmpty()) {
			plate.append("\n  ").append(f[6]).append(' ').append(f[7])
					.append(f[8].equals("1") ? " varargs" : "").append(", returns ").append(f[9]);
		}
		for (String[] v : fvars) {
			String where = v[4];
			if (!v[2].equals("reg")) {
				int bp = Integer.parseInt(where.replace("+", ""));
				where = String.format("[bp%s%d]", bp < 0 ? "-" : "+", Math.abs(bp));
			}
			plate.append(String.format("\n  %-6s %s %s %s", v[2], v[5], v[3], where));
		}
		setPlateComment(addr, plate.toString());

		if (!f[6].isEmpty()) {
			try {
				applyPrototype(fn, f, fvars);
				prototypes++;
			}
			catch (Exception e) {
				printerr("prototype " + f[2] + ": " + e.getMessage());
				failed++;
			}
		}
		applyLocals(fn, fvars);
	}

	private void applyPrototype(Function fn, String[] f, List<String[]> fvars) throws Exception {
		boolean far = f[7].equals("far");
		// Ghidra has no 16-bit pascal convention; stdcall16 gives the callee-pops-arguments
		// behaviour, and the custom storage below puts each argument at its real offset.
		String cc = (f[6].equals("pascal") ? "__stdcall16" : "__cdecl16") + (far ? "far" : "near");

		List<Parameter> params = new ArrayList<>();
		Set<String> names = new HashSet<>();
		for (String[] v : fvars) {
			if (!v[2].equals("param") || !names.add(v[3])) {
				continue;
			}
			DataType dt = parseType(v[5]);
			if (dt == null || dt.getLength() <= 0) {
				dt = Undefined2DataType.dataType;
			}
			int bp = Integer.parseInt(v[4].replace("+", ""));
			// Ghidra stack offsets are relative to SP at entry; BP = entry SP - 2.
			VariableStorage storage = new VariableStorage(currentProgram, bp - 2, dt.getLength());
			params.add(new ParameterImpl(v[3], dt, storage, currentProgram, SourceType.IMPORTED));
		}
		// Declaration order: cdecl pushes right to left (first parameter at the lowest offset),
		// pascal pushes left to right (first parameter at the highest offset).
		boolean pascal = f[6].equals("pascal");
		params.sort((a, b) -> pascal ? b.getStackOffset() - a.getStackOffset()
				: a.getStackOffset() - b.getStackOffset());

		DataType ret = parseType(f[9]);
		if (ret == null) {
			ret = Undefined2DataType.dataType;
		}
		VariableStorage retStorage = returnStorage(ret);
		ReturnParameterImpl retParam = new ReturnParameterImpl(ret, retStorage, currentProgram);

		fn.updateFunction(cc, retParam, params, FunctionUpdateType.CUSTOM_STORAGE, true,
			SourceType.IMPORTED);
		fn.setVarArgs(f[8].equals("1"));
		if (fn.getParameterCount() != params.size()) {
			throw new IllegalStateException("Ghidra kept " + fn.getParameterCount() + " of " +
				params.size() + " parameters");
		}
	}

	private VariableStorage returnStorage(DataType ret) throws Exception {
		Register al = currentProgram.getRegister("AL"), ax = currentProgram.getRegister("AX");
		Register dx = currentProgram.getRegister("DX");
		return switch (ret instanceof VoidDataType ? 0 : ret.getLength()) {
			case 0 -> VariableStorage.VOID_STORAGE;
			case 1 -> new VariableStorage(currentProgram, al);
			case 2 -> new VariableStorage(currentProgram, ax);
			case 4 -> new VariableStorage(currentProgram, dx, ax);
			default -> VariableStorage.UNASSIGNED_STORAGE;
		};
	}

	private void applyLocals(Function fn, List<String[]> fvars) {
		StackFrame frame = fn.getStackFrame();
		Set<Integer> done = new HashSet<>();
		Set<String> names = new HashSet<>();
		for (Parameter p : fn.getParameters()) {
			names.add(p.getName());
		}
		for (String[] v : fvars) {
			if (!v[2].equals("local")) {
				continue;
			}
			int off = Integer.parseInt(v[4].replace("+", "")) - 2;
			DataType dt = parseType(v[5]);
			if (dt == null || dt.getLength() <= 0 || !done.add(off)) {
				continue; // a block-scope local sharing a slot with an earlier one
			}
			String name = v[3];
			for (int n = 2; !names.add(name); n++) {
				name = v[3] + "_" + n;
			}
			try {
				for (Variable old : frame.getLocals()) {
					int a = old.getStackOffset(), b = a + old.getLength();
					if (a < off + dt.getLength() && off < b) {
						frame.clearVariable(a);
					}
				}
				frame.createVariable(name, off, dt, SourceType.IMPORTED);
				locals++;
			}
			catch (Exception e) {
				printerr("local " + fn.getName() + "." + name + ": " + e.getMessage());
				failed++;
			}
		}
	}

	private void applyCode(Address addr, String name) throws Exception {
		Function fn = getFunctionAt(addr);
		if (fn == null && !name.endsWith("@") && isCallTarget(addr)) {
			if (makeFunction(addr, name) != null) {
				return;
			}
		}
		if (fn != null) {
			if (!fn.getName().equals(name)) {
				fn.setName(name, SourceType.IMPORTED);
				renamed++;
			}
			return;
		}
		label(addr, name);
	}

	private boolean isCallTarget(Address addr) {
		for (Reference ref : getReferencesTo(addr)) {
			if (ref.getReferenceType().isCall()) {
				return true;
			}
		}
		return false;
	}

	// ---- data ----

	/**
	 * DGROUP holds no code, but analysis creates functions there from misread jump tables
	 * and from a few near CALLs whose 16-bit target it doesn't wrap. Remove the ones analysis
	 * made (default names) so the globals the debug info places there can be typed.
	 */
	private void removeDataSegmentFunctions(List<String[]> rows) throws Exception {
		MemoryBlock dgroup = null;
		for (String[] f : rows) {
			if (f[0].equals("DATA") && f[2].equals("DATASEG@")) {
				dgroup = currentProgram.getMemory().getBlock(toAddr(f[1]));
			}
		}
		if (dgroup == null) {
			return;
		}
		List<Function> bogus = new ArrayList<>();
		for (Function fn : currentProgram.getFunctionManager()
				.getFunctions(new AddressSet(dgroup.getStart(), dgroup.getEnd()), true)) {
			if (fn.getSymbol().getSource() == SourceType.DEFAULT ||
				fn.getName().startsWith("caseD_")) {
				bogus.add(fn);
			}
		}
		for (Function fn : bogus) {
			AddressSetView body = fn.getBody();
			println("removing analysis-made function " + fn.getName() + " at " +
				fn.getEntryPoint() + " in DGROUP");
			removeFunction(fn);
			clearListing(body);
		}
	}

	private void applyData(Address addr, String name, String type) throws Exception {
		label(addr, name);
		DataType dt = parseType(type);
		if (dt == null || dt instanceof VoidDataType || dt.getLength() <= 0) {
			return;
		}
		Data existing = getDataAt(addr);
		if (existing != null && existing.getDataType().isEquivalent(dt)) {
			return;
		}
		try {
			// Analysis sometimes disassembles DGROUP bytes into stray instructions; the debug
			// info says they are data, so clear those unless a function claims them.
			Address end = addr.add(dt.getLength() - 1);
			for (Address a = addr; a.compareTo(end) <= 0;) {
				Instruction ins = getInstructionContaining(a);
				if (ins == null) {
					a = a.add(1);
					continue;
				}
				if (getFunctionContaining(ins.getMinAddress()) != null) {
					printerr("data " + name + " overlaps code in " +
						getFunctionContaining(ins.getMinAddress()).getName());
					failed++;
					return;
				}
				clearListing(ins.getMinAddress(), ins.getMaxAddress());
				a = ins.getMaxAddress().add(1);
			}

			DataUtilities.createData(currentProgram, addr, dt, -1,
				DataUtilities.ClearDataMode.CLEAR_ALL_CONFLICT_DATA);
			typedData++;
		}
		catch (Exception e) {
			printerr("data " + name + ": " + e.getMessage());
			failed++;
		}
	}

	private void label(Address addr, String name) throws Exception {
		for (Symbol s : currentProgram.getSymbolTable().getSymbols(addr)) {
			if (s.getName().equals(name)) {
				if (!s.isPrimary()) {
					s.setPrimary();
				}
				return;
			}
		}
		Symbol s = createLabel(addr, name, true, SourceType.IMPORTED);
		if (s == null) {
			failed++;
			return;
		}
		labeled++;
	}

	// ---- source lines ----

	private void applyLine(SourceFileManager sfm, Map<String, SourceFile> files, Address addr,
			String[] f) throws Exception {
		long length = Long.parseLong(f[2]);
		int line = Integer.parseInt(f[4]);
		if (length <= 0) {
			return;
		}
		SourceFile sf = files.get(f[3]);
		if (sf == null) {
			sf = new SourceFile("/" + f[3]);
			sfm.addSourceFile(sf);
			files.put(f[3], sf);
		}
		for (SourceMapEntry e : sfm.getSourceMapEntries(addr)) {
			if (e.getSourceFile().equals(sf) && e.getLineNumber() == line) {
				return;
			}
		}
		try {
			sfm.addSourceMapEntry(sf, line, addr, length);
			lines++;
		}
		catch (Exception e) {
			failed++;
		}
	}
}
