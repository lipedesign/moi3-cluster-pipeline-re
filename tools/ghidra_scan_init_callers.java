// ghidra_scan_init_callers.java — scan every instruction for a call/branch whose flow target is
// Rgba2yuv::init (0x121ec8), to find the OMX-side function that triggers the shader/FBO setup.
// getReferencesTo misses thunked calls, so we walk instruction flows. @category MOI3
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.util.task.ConsoleTaskMonitor;
import java.util.HashSet;
import java.util.Set;

public class ghidra_scan_init_callers extends GhidraScript {
    @Override
    public void run() throws Exception {
        long target = 0x121030L;  /* the thunk/PLT stub that jumps to Rgba2yuv::init */
        FunctionManager fm = currentProgram.getFunctionManager();
        Set<Long> callers = new HashSet<>();
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            Address[] flows = ins.getFlows();
            if (flows == null) continue;
            for (Address fa : flows) {
                if (fa.getOffset() == target) {
                    Function f = fm.getFunctionContaining(ins.getAddress());
                    if (f != null) callers.add(f.getEntryPoint().getOffset());
                    else println("call to init @ " + ins.getAddress() + " (no function)");
                }
            }
        }
        println("caller functions of init: " + callers.size());
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        ConsoleTaskMonitor mon = new ConsoleTaskMonitor();
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
