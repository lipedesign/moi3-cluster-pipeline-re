#!/usr/bin/env python3
# extract_wl_protocol.py — recover Wayland interface definitions (wl_interface / wl_message)
# from a stripped ELF64 (LE) by walking .data.rel.ro and resolving R_*_RELATIVE relocations.
#
# Wayland client/server stubs embed, in read-only data, a `struct wl_interface` per protocol
# interface: { const char *name; int version; int method_count; const wl_message *methods;
# int event_count; const wl_message *events; }, and each `struct wl_message` is
# { const char *name; const char *signature; const wl_interface **types; }. Recovering these
# gives the exact request/event names, opcodes (array order) and argument signatures — which is
# what you need to implement the other side of the protocol.
#
# This is a generic ELF reader; it contains no proprietary data. It does not execute the target.
# Usage:  python3 -I extract_wl_protocol.py <elf> [substr ...]
#         (no substr => print every interface whose name contains "ivi" or "share")
import sys, struct

def main():
    if len(sys.argv) < 2:
        print("usage: extract_wl_protocol.py <elf> [name-substr ...]"); return 2
    path = sys.argv[1]
    wanted = sys.argv[2:]
    with open(path, 'rb') as f:
        data = f.read()
    if data[:4] != b'\x7fELF' or data[4] != 2 or data[5] != 1:
        print("not a little-endian ELF64"); return 2

    e_shoff = struct.unpack_from('<Q', data, 0x28)[0]
    e_shentsize = struct.unpack_from('<H', data, 0x3a)[0]
    e_shnum = struct.unpack_from('<H', data, 0x3c)[0]
    e_shstrndx = struct.unpack_from('<H', data, 0x3e)[0]

    secs = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        nm, typ, flags, addr, offset, size, link, info, align, entsz = \
            struct.unpack_from('<IIQQQQIIQQ', data, off)
        secs.append(dict(nameoff=nm, typ=typ, addr=addr, offset=offset, size=size))
    sh = secs[e_shstrndx]
    def secname(s):
        o = sh['offset'] + s['nameoff']; return data[o:data.index(b'\0', o)].decode()
    for s in secs:
        s['nm'] = secname(s)
    byname = {s['nm']: s for s in secs}

    progbits = [s for s in secs if s['addr'] and s['size'] and s['typ'] == 1]
    def vma_to_fileoff(vma):
        for s in progbits:
            if s['addr'] <= vma < s['addr'] + s['size']:
                return s['offset'] + (vma - s['addr'])
        return None
    def read_cstr(vma):
        o = vma_to_fileoff(vma)
        if o is None: return None
        e = data.index(b'\0', o); return data[o:e].decode('latin1')

    reloc = {}
    rela = byname.get('.rela.dyn')
    if rela:
        for i in range(rela['size'] // 24):
            o = rela['offset'] + i * 24
            r_off, r_info, r_add = struct.unpack_from('<QQq', data, o)
            if (r_info & 0xffffffff) == 1027:  # R_AARCH64_RELATIVE
                reloc[r_off] = r_add
    def read_ptr(vma):
        if vma in reloc: return reloc[vma]
        o = vma_to_fileoff(vma)
        return struct.unpack_from('<Q', data, o)[0] if o is not None else 0

    def msgs(ptr, count):
        out = []
        for i in range(count):
            m = ptr + i * 24
            out.append((read_cstr(read_ptr(m)), read_cstr(read_ptr(m + 8))))
        return out

    drr = byname['.data.rel.ro']
    base, size = drr['addr'], drr['size']
    seen = set()
    for p in range(base, base + size - 40, 8):
        name_ptr = read_ptr(p)
        if not name_ptr: continue
        nm = read_cstr(name_ptr)
        if not nm or not nm.replace('_', '').isalnum() or len(nm) < 3: continue
        if wanted:
            if nm not in wanted: continue
        elif 'ivi' not in nm and 'share' not in nm:
            continue
        fo = drr['offset'] + (p - base)
        version, method_count = struct.unpack_from('<ii', data, fo + 8)
        event_count = struct.unpack_from('<i', data, fo + 24)[0]
        if not (0 <= method_count < 64 and 0 <= event_count < 64): continue
        methods_ptr, events_ptr = read_ptr(p + 16), read_ptr(p + 32)
        key = (nm, version, method_count, event_count)
        if key in seen: continue
        seen.add(key)
        print(f"interface {nm}  (version {version}, {method_count} requests, {event_count} events)")
        for op, (n, s) in enumerate(msgs(methods_ptr, method_count) if methods_ptr else []):
            print(f"    request[{op}]  {n:<28} sig={s!r}")
        for op, (n, s) in enumerate(msgs(events_ptr, event_count) if events_ptr else []):
            print(f"    event[{op}]    {n:<28} sig={s!r}")
        print()

if __name__ == '__main__':
    raise SystemExit(main())
