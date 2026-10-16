// ghidra_callers_init.java — decompile the callers of Rgba2yuv::init (@0x121ec8), i.e. Rgba2yuv::convert,
// to see the gate that decides whether init (shader build) runs. @category MOI3
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.util.task.ConsoleTaskMonitor;
import java.util.HashSet;
import java.util.Set;

public class ghidra_callers_init extends GhidraScript {
    @Override
    public void run() throws Exception {
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        ConsoleTaskMonitor mon = new ConsoleTaskMonitor();
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager rm = currentProgram.getReferenceManager();
        Address init = toAddr(0x121ec8L);
        Set<Long> callers = new HashSet<>();
        for (Reference r : rm.getReferencesTo(init)) {
            Function f = fm.getFunctionContaining(r.getFromAddress());
            if (f != null) callers.add(f.getEntryPoint().getOffset());
        }
        println("callers of init: " + callers.size());
        for (Long off : callers) {
            Function f = fm.getFunctionAt(toAddr(off));
            if (f == null) continue;
            println("\n==================== " + f.getName() + " @ " + f.getEntryPoint() + " ====================");
            DecompileResults dr = dec.decompileFunction(f, 300, mon);
            if (dr != null && dr.decompileCompleted() && dr.getDecompiledFunction() != null)
                println(dr.getDecompiledFunction().getC());
            else println("  [decompile failed]");
        }
        println("######## DONE");
    }
}
