// ghidra_decomp_listener.java — decompile the ivi_share_surface listener callbacks.
// startShareInternal registers `surface_listener_callbacks` (a struct of fn pointers) via
// wl_proxy_add_listener. The per-frame work (gbm_bo_create, EGLImage import, the
// "input buffer or encoder pipeline not ready" check) lives in these callbacks:
// damage / configure / input_capabilities / share_surface_state. @category MOI3
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;
import ghidra.util.task.ConsoleTaskMonitor;
import java.util.HashSet;
import java.util.Set;

public class ghidra_decomp_listener extends GhidraScript {
    @Override
    public void run() throws Exception {
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        ConsoleTaskMonitor mon = new ConsoleTaskMonitor();
        FunctionManager fm = currentProgram.getFunctionManager();
        SymbolTable st = currentProgram.getSymbolTable();
        Address base = null;
        String foundName = null;
        for (Symbol s : st.getAllSymbols(true)) {
            String n = s.getName();
            if (n != null && n.contains("surface_listener")) {
                println("symbol: " + n + " @ " + s.getAddress());
                base = s.getAddress();
                foundName = n;
                break;
            }
        }
        if (base == null) { println("### listener symbol not found"); println("######## DONE"); return; }
        println("### using listener struct @ " + base + " (" + foundName + ")");
        Set<Address> seen = new HashSet<>();
        for (int i = 0; i < 6; i++) {
            Address slot = base.add(i * 8);
            long p;
            try { p = currentProgram.getMemory().getLong(slot); } catch (Exception e) { println("cb[" + i + "] unreadable"); break; }
            if (p == 0) { println("cb[" + i + "] = null (end of struct)"); break; }
            Address fa = toAddr(p);
            Function fn = fm.getFunctionAt(fa);
            if (fn == null) fn = fm.getFunctionContaining(fa);
            if (fn == null) { try { fn = createFunction(fa, null); } catch (Exception e) { fn = null; } }
            if (fn == null) { println("cb[" + i + "] @ " + fa + " (no function, could not create)"); continue; }
            if (!seen.add(fn.getEntryPoint())) { println("cb[" + i + "] -> " + fn.getName() + " (dup)"); continue; }
            println("\n==================== callback[" + i + "] -> " + fn.getName() + " @ " + fn.getEntryPoint() + " ====================");
            DecompileResults dr = dec.decompileFunction(fn, 240, mon);
            if (dr != null && dr.decompileCompleted() && dr.getDecompiledFunction() != null)
                println(dr.getDecompiledFunction().getC());
            else
                println("  [decompile failed]");
        }
        println("######## DONE");
    }
}
