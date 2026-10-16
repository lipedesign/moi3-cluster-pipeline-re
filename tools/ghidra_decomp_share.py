# ghidra_decomp_share.py — Ghidra headless postScript.
# The multi-screen-server is stripped, so we locate the ShareServer functions by cross-reference
# to known log strings and print their decompiled C. Run via analyzeHeadless -postScript.
# @category MOI3
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

prog = getCurrentProgram()
fm = prog.getFunctionManager()
mem = prog.getMemory()
mon = ConsoleTaskMonitor()
dec = DecompInterface()
dec.openProgram(prog)

def decompile(fn, why):
    print("\n==================== %s ====================" % why)
    print("# function %s @ %s" % (fn.getName(), fn.getEntryPoint()))
    r = dec.decompileFunction(fn, 120, mon)
    if r is not None and r.decompileCompleted() and r.getDecompiledFunction() is not None:
        print(r.getDecompiledFunction().getC())
    else:
        msg = r.getErrorMessage() if r is not None else "no result"
        print("  [decompile failed: %s]" % msg)

def find_all(substr):
    out = []
    start = mem.getMinAddress()
    needle = bytearray(substr)
    while True:
        a = mem.findBytes(start, needle, None, True, mon)
        if a is None:
            break
        out.append(a)
        start = a.add(1)
    return out

STRINGS = [
    "input buffer or encoder pipeline not ready",
    "START STREAMING",
    "cb_surface_state begin",
    "startShareInternal",
]

seen = set()
for s in STRINGS:
    addrs = find_all(s)
    print("\n######## string %r -> %d occurrence(s)" % (s, len(addrs)))
    fns = []
    for a in addrs:
        for r in getReferencesTo(a):
            fn = fm.getFunctionContaining(r.getFromAddress())
            if fn is not None and fn.getEntryPoint() not in seen:
                seen.add(fn.getEntryPoint())
                fns.append(fn)
    print("   %d function(s) reference it" % len(fns))
    for fn in fns:
        decompile(fn, "xref to %r" % s)

print("\n######## DONE")
