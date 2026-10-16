// ghidra_decomp_share.java — Ghidra headless postScript (Java, no PyGhidra needed).
// The multi-screen-server is stripped; locate the ShareServer functions by xref to known log
// strings and print their decompiled C. Run via analyzeHeadless -postScript.
// @category MOI3
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;
import ghidra.util.task.ConsoleTaskMonitor;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

public class ghidra_decomp_share extends GhidraScript {
    @Override
    public void run() throws Exception {
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        Memory mem = currentProgram.getMemory();
        FunctionManager fm = currentProgram.getFunctionManager();
        ConsoleTaskMonitor mon = new ConsoleTaskMonitor();
        String[] strs = {
            "input buffer or encoder pipeline not ready",
            "START STREAMING",
            "cb_surface_state begin",
            "startShareInternal",
        };
        Set<Address> seen = new HashSet<>();
        for (String s : strs) {
            println("######## string \"" + s + "\"");
            List<Address> addrs = new ArrayList<>();
            Address start = mem.getMinAddress();
            byte[] needle = s.getBytes();
            while (true) {
                Address a = mem.findBytes(start, needle, null, true, mon);
                if (a == null) break;
                addrs.add(a);
                start = a.add(1);
            }
            println("   occurrences: " + addrs.size());
            for (Address a : addrs) {
                Reference[] refs = getReferencesTo(a);
                for (Reference r : refs) {
                    Function fn = fm.getFunctionContaining(r.getFromAddress());
                    if (fn == null || seen.contains(fn.getEntryPoint())) continue;
                    seen.add(fn.getEntryPoint());
                    println("\n==================== xref to \"" + s + "\" ====================");
                    println("# function " + fn.getName() + " @ " + fn.getEntryPoint());
                    DecompileResults dr = dec.decompileFunction(fn, 120, mon);
                    if (dr != null && dr.decompileCompleted() && dr.getDecompiledFunction() != null)
                        println(dr.getDecompiledFunction().getC());
                    else
                        println("  [decompile failed: " + (dr != null ? dr.getErrorMessage() : "null") + "]");
                }
            }
        }
        println("######## DONE");
    }
}
