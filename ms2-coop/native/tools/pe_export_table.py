#!/usr/bin/env python3
"""Dump a PE export table, distinguishing real code from forwarder strings.

Why this matters for a proxy DLL
--------------------------------
A proxy that replaces `dxgi.dll` must re-export everything the real one does,
because Windows resolves a dependency by name through the *executable's*
directory first.  So once our DLL takes the name, every module in the process
that imports `dxgi.dll` -- including System32's own `d3d11.dll` -- resolves to
us instead.  Missing one export breaks a loader that we never touch directly.

There are two ways to re-export:
  * a forwarder string (`OtherDll.Function`), which the loader resolves without
    any code of ours, or
  * a real stub function.
An export entry is a forwarder when its "function RVA" actually points inside
the export directory's own address range -- then it is a string, not code.

Usage: python tools/pe_export_table.py DLL [--forwarders-only]
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe_imports import PE  # noqa: E402


def dump(path, forwarders_only=False):
    pe = PE(path)
    if not pe.ok:
        print("PARSE FAILED: %s" % "; ".join(pe.notes))
        return 1
    with open(path, "rb") as fh:
        data = fh.read()

    exp_rva, exp_size = pe.dd[0]
    if exp_rva == 0:
        print("%s: no export directory" % path)
        return 1
    off = pe.rva_to_off(exp_rva)
    (chars, ts, major, minor, name_rva, base, nfunc, nname,
     addr_funcs, addr_names, addr_ords) = struct.unpack_from("<IIHHIIIIIII", data, off)

    nm = pe.rva_to_off(name_rva)
    dll_name = pe.cstr(nm) if nm else "?"
    print("FILE      : %s" % path)
    print("DLL NAME  : %s" % dll_name)
    print("EXPORTS   : %d named, %d total, ordinal base %d" % (nname, nfunc, base))
    print()

    foff = pe.rva_to_off(addr_funcs)
    noff = pe.rva_to_off(addr_names)
    ooff = pe.rva_to_off(addr_ords)
    lo, hi = exp_rva, exp_rva + exp_size

    rows = []
    for i in range(nname):
        (nrva,) = struct.unpack_from("<I", data, noff + i * 4)
        (ordinal,) = struct.unpack_from("<H", data, ooff + i * 2)
        (frva,) = struct.unpack_from("<I", data, foff + ordinal * 4)
        no = pe.rva_to_off(nrva)
        name = pe.cstr(no) if no is not None else "?"
        if lo <= frva < hi:
            fo = pe.rva_to_off(frva)
            target = pe.cstr(fo) if fo is not None else "?"
            rows.append((name, base + ordinal, "FORWARD", target))
        else:
            rows.append((name, base + ordinal, "code", "0x%X" % (pe.image_base + frva)))

    fwd = [r for r in rows if r[2] == "FORWARD"]
    print("forwarders: %d    real code: %d" % (len(fwd), len(rows) - len(fwd)))
    print()
    for name, ordinal, kind, target in (fwd if forwarders_only else rows):
        print("  ord %-5d %-46s %-8s %s" % (ordinal, name, kind, target))
    if fwd and not forwarders_only:
        print()
        print("-- forwarder targets (distinct) --")
        for t in sorted({r[3] for r in fwd}):
            print("   %s" % t)
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dll")
    ap.add_argument("--forwarders-only", action="store_true")
    args = ap.parse_args()
    return dump(args.dll, args.forwarders_only)


if __name__ == "__main__":
    sys.exit(main())
