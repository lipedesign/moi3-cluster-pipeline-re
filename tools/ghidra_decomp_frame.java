// ghidra_decomp_frame.java — decompile the frame-loop: every function that CALLS gbm_bo_create /
// gbm_bo_import (robust against PLT thunks; getReferencesTo on the external symbol misses them).
// Java postScript, no PyGhidra. @category MOI3
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.util.task.ConsoleTaskMonitor;
import java.util.HashSet;
import java.util.Set;

public class ghidra_decomp_frame extends GhidraScript {
    @Override
    public void run() throws Exception {
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        ConsoleTaskMonitor mon = new ConsoleTaskMonitor();
        FunctionManager fm = currentProgram.getFunctionManager();
        Set<Address> seen = new HashSet<>();
        for (Function fn : fm.getFunctions(true)) {
            if (fn.isThunk() || fn.isExternal()) continue;
            String why = null;
            for (Function callee : fn.getCalledFunctions(mon)) {
                String n = callee.getName();
                if (n.contains("gbm_bo_create") || n.contains("gbm_bo_import") || n.contains("EGLImageTargetTexture")) {
                    why = n; break;
                }
            }
            if (why == null || !seen.add(fn.getEntryPoint())) continue;
            println("\n==================== calls " + why + " ====================");
            println("# function " + fn.getName() + " @ " + fn.getEntryPoint());
            DecompileResults dr = dec.decompileFunction(fn, 180, mon);
            if (dr != null && dr.decompileCompleted() && dr.getDecompiledFunction() != null)
                println(dr.getDecompiledFunction().getC());
            else
                println("  [decompile failed]");
        }
        println("######## DONE");
    }
}
