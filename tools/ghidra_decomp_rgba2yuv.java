// ghidra_decomp_rgba2yuv.java — decompile Rgba2yuv's constructor and convert (and any method whose
// name contains "Rgba2yuv"), to find why the shader program is not built in standalone (the mss
// calls glUseProgram(0) and never glCreateProgram). @category MOI3
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.util.task.ConsoleTaskMonitor;
import java.util.HashSet;
import java.util.Set;

public class ghidra_decomp_rgba2yuv extends GhidraScript {
    @Override
    public void run() throws Exception {
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        ConsoleTaskMonitor mon = new ConsoleTaskMonitor();
        FunctionManager fm = currentProgram.getFunctionManager();
        Set<Long> seen = new HashSet<>();
        for (Function fn : fm.getFunctions(true)) {
            String n = fn.getName();
            if (n == null) continue;
            boolean want = n.contains("Rgba2yuv") || n.contains("rgba2yuv") || n.contains("Rgba2Yuv");
            if (!want) continue;
            if (!seen.add(fn.getEntryPoint().getOffset())) continue;
            println("\n==================== " + fn.getName() + " @ " + fn.getEntryPoint() + " ====================");
            DecompileResults dr = dec.decompileFunction(fn, 240, mon);
            if (dr != null && dr.decompileCompleted() && dr.getDecompiledFunction() != null)
                println(dr.getDecompiledFunction().getC());
            else
                println("  [decompile failed]");
        }
        println("######## DONE");
    }
}
