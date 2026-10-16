// ghidra_find_shader.java — find the GLSL shader source strings and decompile the function(s) that
// reference them (the lazy shader builder inside Rgba2yuv::convert). Also dumps the GLSL source so we
// can see what the shader expects. @category MOI3
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.util.task.ConsoleTaskMonitor;
import java.util.HashSet;
import java.util.Set;

public class ghidra_find_shader extends GhidraScript {
    @Override
    public void run() throws Exception {
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        ConsoleTaskMonitor mon = new ConsoleTaskMonitor();
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager rm = currentProgram.getReferenceManager();
        String[] marks = {"samplerExternalOES", "gl_FragColor", "gl_Position", "precision", "varying", "attribute ", "void main", "#version", "mediump", "texture2D"};
        Set<Long> fns = new HashSet<>();
        DataIterator it = currentProgram.getListing().getDefinedData(true);
        int shown = 0;
        while (it.hasNext()) {
            Data d = it.next();
            if (d == null) continue;
            Object v = d.getValue();
            if (!(v instanceof String)) continue;
            String s = (String) v;
            boolean glsl = false;
            for (String m : marks) if (s.contains(m)) { glsl = true; break; }
            if (!glsl) continue;
            if (shown < 8) { println("\n--- GLSL string @ " + d.getAddress() + " ---\n" + (s.length() > 600 ? s.substring(0, 600) + "..." : s)); shown++; }
            for (Reference r : rm.getReferencesTo(d.getAddress())) {
                Function f = fm.getFunctionContaining(r.getFromAddress());
                if (f != null) fns.add(f.getEntryPoint().getOffset());
            }
        }
        println("\n######## functions referencing GLSL strings: " + fns.size());
        for (Long off : fns) {
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
