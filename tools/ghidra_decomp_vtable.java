// ghidra_decomp_vtable.java — decompile the AsyncTask thread body: read the vtable at 0x158228
// (the ShareServer AsyncTask subclass vptr) and decompile each virtual method. The frame loop
// (gbm_bo create, EGLImage import, "not ready") lives in one of these. @category MOI3
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.util.task.ConsoleTaskMonitor;
import java.util.HashSet;
import java.util.Set;

public class ghidra_decomp_vtable extends GhidraScript {
    @Override
    public void run() throws Exception {
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        ConsoleTaskMonitor mon = new ConsoleTaskMonitor();
        FunctionManager fm = currentProgram.getFunctionManager();
        long[] vtables = { 0x158288L };
        Set<Address> seen = new HashSet<>();
        for (long vtbase : vtables) {
            for (int i = 0; i < 8; i++) {
                Address slot = toAddr(vtbase + i * 8);
                long p;
                try { p = currentProgram.getMemory().getLong(slot); } catch (Exception e) { break; }
                if (p == 0) continue;
                Address fa = toAddr(p);
                Function fn = fm.getFunctionAt(fa);
                if (fn == null) fn = fm.getFunctionContaining(fa);
                if (fn == null) {
                    try { fn = createFunction(fa, null); } catch (Exception e) { fn = null; }
                }
                if (fn == null) { println("vtable+" + (i * 8) + " -> " + fa + " (no function, could not create)"); continue; }
                if (!seen.add(fn.getEntryPoint())) continue;
                println("\n==================== vtable[" + i + "] (" + slot + ") -> " + fn.getName() + " @ " + fn.getEntryPoint() + " ====================");
                DecompileResults dr = dec.decompileFunction(fn, 200, mon);
                if (dr != null && dr.decompileCompleted() && dr.getDecompiledFunction() != null)
                    println(dr.getDecompiledFunction().getC());
                else
                    println("  [decompile failed]");
            }
        }
        println("######## DONE");
    }
}
