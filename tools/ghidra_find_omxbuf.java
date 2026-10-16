// ghidra_find_omxbuf.java — find and decompile the OMX input-buffer manager: the function(s) that
// reference the "use-buffer mode" / "pipeline not ready" strings and allocate the encoder input
// gbm_bo buffers (and, indirectly, call Rgba2yuv::init). @category MOI3
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.util.task.ConsoleTaskMonitor;
import java.util.HashSet;
import java.util.Set;

public class ghidra_find_omxbuf extends GhidraScript {
    @Override
    public void run() throws Exception {
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        ConsoleTaskMonitor mon = new ConsoleTaskMonitor();
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager rm = currentProgram.getReferenceManager();
        String[] marks = {"use-buffer", "use buffer", "pipeline not ready", "GBM buffers",
                          "input buffer", "UseBuffer", "nBufferCountActual", "buffer count"};
        Set<Long> fns = new HashSet<>();
        DataIterator it = currentProgram.getListing().getDefinedData(true);
        while (it.hasNext()) {
            Data d = it.next();
            if (d == null) continue;
            Object v = d.getValue();
            if (!(v instanceof String)) continue;
            String s = (String) v;
            boolean hit = false;
            for (String m : marks) if (s.contains(m)) { hit = true; break; }
            if (!hit) continue;
            println("--- string @ " + d.getAddress() + ": " + (s.length() > 120 ? s.substring(0, 120) : s));
            for (Reference r : rm.getReferencesTo(d.getAddress())) {
                Function f = fm.getFunctionContaining(r.getFromAddress());
                if (f != null) fns.add(f.getEntryPoint().getOffset());
            }
        }
        println("\n######## functions: " + fns.size());
        for (Long off : fns) {
            Function f = fm.getFunctionAt(toAddr(off));
            if (f == null) continue;
            println("\n==================== " + f.getName() + " @ " + f.getEntryPoint() + " ====================");
            DecompileResults dr = dec.decompileFunction(f, 400, mon);
            if (dr != null && dr.decompileCompleted() && dr.getDecompiledFunction() != null)
                println(dr.getDecompiledFunction().getC());
            else println("  [decompile failed]");
        }
        println("######## DONE");
    }
}
