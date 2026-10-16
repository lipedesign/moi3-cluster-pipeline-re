#!/usr/bin/env python3
# xref.py — find where an ARM64 ELF loads the address of a given string, by scanning .text for the
# adrp+add pair that computes the string's VMA. Prints the code addresses (callers) so you can
# then disassemble the surrounding function. Hand-rolled (no deps); the target is read-only.
# Usage: python3 -I xref.py <elf> <string-substr> [more-substr ...]
import sys, struct

def main():
    path = sys.argv[1]; wants = sys.argv[2:]
    data = open(path, "rb").read()
    e_shoff = struct.unpack_from("<Q", data, 0x28)[0]
    e_shentsize = struct.unpack_from("<H", data, 0x3a)[0]
    e_shnum = struct.unpack_from("<H", data, 0x3c)[0]
    e_shstrndx = struct.unpack_from("<H", data, 0x3e)[0]
    secs = []
    for i in range(e_shnum):
        o = e_shoff + i * e_shentsize
        nm, typ, fl, addr, off, size = struct.unpack_from("<IIQQQQ", data, o)[:6]
        secs.append(dict(nm=nm, typ=typ, addr=addr, off=off, size=size))
    sh = secs[e_shstrndx]
    def sname(s):
        p = sh["off"] + s["nm"]; return data[p:data.index(b"\0", p)].decode()
    for s in secs: s["name"] = sname(s)
    byname = {s["name"]: s for s in secs}
    rod = byname[".rodata"]; txt = byname[".text"]

    # locate each wanted string's VMA
    targets = {}
    blob = data[rod["off"]:rod["off"] + rod["size"]]
    for w in wants:
        wb = w.encode()
        idx = blob.find(wb)
        if idx < 0: print(f"[string not found] {w!r}"); continue
        vma = rod["addr"] + idx
        targets[vma] = w
        print(f"string {w!r} @ vma {vma:#x}")
    if not targets: return

    # scan .text for adrp (rd<-page) then a nearby add xrd,xrd,#lo12 that hits a target
    code = data[txt["off"]:txt["off"] + txt["size"]]
    base_addr = txt["addr"]
    pend = {}  # reg -> (page, pc_of_adrp)
    hits = {}
    n = len(code) // 4
    for i in range(n):
        pc = base_addr + i * 4
        w = struct.unpack_from("<I", code, i * 4)[0]
        if (w & 0x9f000000) == 0x90000000:  # ADRP
            immlo = (w >> 29) & 3; immhi = (w >> 5) & 0x7ffff
            imm = (immhi << 2) | immlo
            if imm & (1 << 20): imm -= (1 << 21)
            rd = w & 0x1f
            pend[rd] = ((pc & ~0xfff) + (imm << 12), pc)
        elif ((w >> 24) & 0x7f) == 0x11:  # ADD immediate
            rd = w & 0x1f; rn = (w >> 5) & 0x1f
            imm12 = (w >> 10) & 0xfff
            if (w >> 22) & 1: imm12 <<= 12
            if rn in pend:
                tgt = pend[rn][0] + imm12
                if tgt in targets:
                    hits.setdefault(targets[tgt], []).append(pend[rn][1])
    for w in wants:
        hs = hits.get(w, [])
        print(f"\n== {w!r}: {len(hs)} xref(s)")
        for a in hs[:8]: print(f"   loaded at code {a:#x}")

if __name__ == "__main__":
    raise SystemExit(main())
