// Decompile selected functions to C files.
// Args: <outDir> <target>...   target = 0xADDR | name | 0xSTART-0xEND (every function in range)
// @category Fable3
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;
import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;

public class DecompileFuncs extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) { println("usage: DecompileFuncs <outDir> <addr|name|start-end>..."); return; }
        File outDir = new File(args[0]);
        outDir.mkdirs();
        DecompInterface dec = new DecompInterface();
        if (!dec.openProgram(currentProgram)) { println("openProgram failed: " + dec.getLastMessage()); return; }
        FunctionManager fm = currentProgram.getFunctionManager();
        List<Function> todo = new ArrayList<>();
        for (int i = 1; i < args.length; i++) {
            String t = args[i];
            if (t.contains("-")) {
                String[] ab = t.split("-");
                Address a = toAddr(ab[0].replace("0x", "")), b = toAddr(ab[1].replace("0x", ""));
                FunctionIterator it = fm.getFunctions(new AddressSet(a, b), true);
                while (it.hasNext()) todo.add(it.next());
                continue;
            }
            String key = t.startsWith("0x") ? t.substring(2) : t;
            Function f = null;
            if (key.matches("[0-9A-Fa-f]{6,8}")) {
                Address a = toAddr(key);
                f = getFunctionAt(a);
                if (f == null) f = fm.getFunctionContaining(a);
            } else {
                List<Function> fs = getGlobalFunctions(t);
                if (!fs.isEmpty()) f = fs.get(0);
            }
            if (f == null) { println("NOT FOUND: " + t); continue; }
            todo.add(f);
        }
        int ok = 0, fail = 0;
        for (Function f : todo) {
            if (monitor.isCancelled()) break;
            DecompileResults r = dec.decompileFunction(f, 60, monitor);
            if (r == null || !r.decompileCompleted()) { fail++; continue; }
            String safe = ("fn_" + f.getEntryPoint().toString().toUpperCase()).replaceAll("[^A-Za-z0-9_]", "_");
            PrintWriter pw = new PrintWriter(new File(outDir, safe + ".c"));
            pw.print(r.getDecompiledFunction().getC());
            pw.close();
            ok++;
        }
        println("DECOMPILED " + ok + " functions, " + fail + " failed, into " + outDir.getPath());
        dec.dispose();
    }
}
