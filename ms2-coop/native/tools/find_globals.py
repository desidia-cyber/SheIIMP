#!/usr/bin/env python3
"""Recover ground truth for the UE5.6 globals from a real session's memory dump.

Nothing here guesses: the dump is a captured address space of the shipping exe,
so every global we can identify structurally gives us an RVA that is exact for
this build, and every object we can walk proves the member offsets we are about
to compile into the DLL.

Step 1 -- find GUObjectArray by its STRUCTURE.  FChunkedFixedUObjectArray is 0x20
bytes of very distinctive shape (two pointers, then four int32 where
MaxElements is a multiple of 64K and NumElements <= MaxElements).  Searching the
exe's writable section for that shape finds it without knowing any address.
"""
import struct
import sys

from minidump import Dump

EXE_FRAG = "MortalShell2-Win64-Shipping"

# Section RVAs read out of the exe by tools/pe_imports.py (same file as the dump).
SECTIONS = [
    (".text", 0x1000, 0x86F641C),
    (".rdata", 0x86F8000, 0x2613F5C),
    (".data", 0xAD0C000, 0x645034),
    (".pdata", 0xB352000, 0x6CEDF0),
]


def rva_of(dump, exe, addr):
    return addr - exe["base"]


def find_chunked_arrays(dump, exe):
    """Yield (addr, fields) for every FChunkedFixedUObjectArray-shaped blob."""
    sect = [s for s in SECTIONS if s[0] == ".data"][0]
    start = exe["base"] + sect[1]
    end = start + sect[2]
    out = []
    addr = start & ~7
    while addr < end:
        blob = dump.read(addr, 0x20)
        if blob is None:
            addr += 8
            continue
        objects, items, maxel, numel, maxch, nch = struct.unpack("<QQiiii", blob)
        if (objects and items
                and 0 < maxel <= 0x8000000 and maxel % 0x10000 == 0
                and 0 < numel <= maxel
                and 0 < maxch <= 4096
                and 0 < nch <= maxch):
            out.append((addr, objects, items, maxel, numel, maxch, nch))
        addr += 8
    return out


def main():
    dump = Dump(sys.argv[1])
    exe = dump.module_named(EXE_FRAG)
    if not exe:
        raise SystemExit("exe not in dump")
    base = exe["base"]
    print("exe base   : 0x%016X  size=0x%X  ts=0x%08X" % (base, exe["size"], exe["timestamp"]))

    cands = find_chunked_arrays(dump, exe)
    print("\nFChunkedFixedUObjectArray-shaped blobs in .data: %d" % len(cands))
    for c in cands[:12]:
        addr, objects, items, maxel, numel, maxch, nch = c
        print("  at 0x%016X (exe+0x%X)" % (addr, addr - base))
        print("     Objects=0x%016X ObjectItems=0x%016X MaxElements=%d NumElements=%d "
              "MaxChunks=%d NumChunks=%d" % (objects, items, maxel, numel, maxch, nch))

    if not cands:
        return

    # The global FUObjectArray -- if the first candidate is right -- starts 0x10
    # bytes before the chunked array (FUObjectArray: 3 int32 + bool, then the
    # array at +0x10).
    for c in cands[:4]:
        addr = c[0]
        head = dump.read(addr - 0x10, 0x10)
        h = struct.unpack("<iiiB", head)
        print("\n  candidate global at exe+0x%X: prefix ints=%s" % (addr - base - 0x10, h))
        # walk a few objects and print their class/name
        objects, items, maxel, numel = c[1], c[2], c[3], c[4]
        for i in range(0, min(numel, 6)):
            probe_objects(dump, base, i, objects, items)


def obj_from_array(dump, objects, items, idx):
    if idx < 0:
        return None
    chunk = idx // 0x10000
    if chunk == 0:
        s0 = dump.u64(objects + idx * 0x18)
    else:
        chunkptr = dump.u64(items + chunk * 8)
        if not chunkptr:
            return None
        s0 = dump.u64(chunkptr + (idx % 0x10000) * 0x18)
    return s0


def probe_objects(dump, base, i, objects, items):
    item = objects + i * 0x18
    blob = dump.read(item, 0x18)
    if not blob:
        print("    [%d] item unreadable" % i)
        return
    obj, flags, serial, idx = struct.unpack("<QiIi", blob)
    if not obj:
        return
    o = dump.read(obj, 0x40)
    if not o:
        print("    [%d] obj 0x%X unreadable" % (i, obj))
        return
    vt, objflags, internal, cls, name_ci, name_num, outer = struct.unpack("<QQiQIIQ", o)
    print("    [%d] obj=0x%X vtable=exe+0x%X flags=0x%X idx=%d class=0x%X name=(%d,%d) outer=0x%X"
          % (i, obj, vt - base, objflags, internal, cls, name_ci, name_num, outer))


if __name__ == "__main__":
    main()
