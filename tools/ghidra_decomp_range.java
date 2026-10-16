// ghidra_decomp_range.java — decompile every function whose entry is in [0x121000, 0x122600),
// i.e. the Rgba2yuv methods (constructor/destructor/init are here; convert should be too). @category MOI3
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.util.task.ConsoleTaskMonitor;

public class ghidra_decomp_range extends GhidraScript {
    @Override
    public void run() throws Exception {
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        ConsoleTaskMonitor mon = new ConsoleTaskMonitor();
        FunctionManager fm = currentProgram.getFunctionManager();
        long lo = 0x121000L, hi = 0x122600L;
        for (Function fn : fm.getFunctions(true)) {
            long off = fn.getEntryPoint().getOffset();
            if (off < lo || off >= hi) continue;
            println("\n==================== " + fn.getName() + " @ " + fn.getEntryPoint() + " ====================");
            DecompileResults dr = dec.decompileFunction(fn, 200, mon);
            if (dr != null && dr.decompileCompleted() && dr.getDecompiledFunction() != null)
                println(dr.getDecompiledFunction().getC());
            else println("  [decompile failed]");
        }
        println("######## DONE");
    }
}
