#!/usr/bin/env python3
"""Recover the ground-truth RVAs of the UE5.6 engine globals for this exe.

THE PROBLEM.  UE4SS logged absolute runtime addresses of the engine globals
(GUObjectArray, GNatives, GMalloc, FName::ToString, ProcessEvent, ...) for a real
session.  Their *differences* are facts about the binary and are base-independent,
but a single absolute address is not, so on its own none of them gives an RVA.

THE LEVER.  Two more pure facts about the image:

  * .pdata is the x64 unwind table: every one of its RUNTIME_FUNCTION entries names
    a function's begin RVA.  So "is RVA X the start of a function?" is answerable
    from the file, for every X.
  * section headers say which RVA ranges are code (.text) and which are writable
    data (.data).

Six of the logged addresses are function entry points and two are globals.  So
for each 64K-aligned candidate base in the window the section layout allows, the
induced RVAs either all land on function starts / in .data, or they do not.  The
intersection is the base -- and it is almost always a single value.

Once the base is known every logged address becomes an exact RVA for THIS build,
which is what lets the DLL hard-code nothing and still be verifiable: the AOB
patterns it scans for are checked, offline and here, to resolve to exactly these
addresses.

Read-only.  Prints a report; writes nothing but stdout.
"""
import struct
import sys

EXE = r"E:\Games\_MS2-coop-test\MortalShell2\Binaries\Win64\MortalShell2-Win64-Shipping.exe"

# Absolute addresses from UE4SS 3.0.1's log (tools/ue4ss/dists ... the session
# logs kept at ms2-coop/local/lab/20261022-142616-sp15/UE4SS.log and
# E:\Games\_MS2-coop-test\...\UE4SS.log).  Both sessions agree to the byte, which
# is why they can be treated as one observation.
LOGGED = {
    # name                      address                kind
    "FName::ToString":          (0x7FF7181C5610, "func"),
    "FName::FName(wchar_t*)":   (0x7FF7181B4900, "func"),
    "FUObjectHashTables::Get":  (0x7FF71840F990, "func"),
    "ConsoleManagerSingleton":  (0x7FF7180333A0, "func"),   # PS resolves a getter
    "StaticConstructObject":    (0x7FF7184040B0, "func"),
    "ProcessConsoleExec":       (0x7FF71823B100, "func"),
    "UStruct::Link":            (0x7FF7182AD060, "func"),
    "GameEngine::LoadMap":      (0x7FF71B346E00, "func"),
    "ProcessEvent":             (0x7FF7183D9320, "func"),
    "ProcessInternal":          (0x7FF7183D9710, "func"),
    "ProcessLocalScriptFunc":   (0x7FF7183D9810, "func"),
    "GameEngineTick":           (0x7FF71AB67B30, "func"),   # vtable-addressed
    "GUObjectArray":            (0x7FF721DFAEE0, "data"),
    "GNatives":                 (0x7FF721DF9970, "data"),
    "GMalloc":                  (0x7FF721D0B3C0, "data"),
}


def sections_of(path):
    data = open(path, "rb").read()
    (e_lfanew,) = struct.unpack_from("<I", data, 0x3C)
    if data[e_lfanew:e_lfanew + 4] != b"PE\0\0":
        raise SystemExit("not a PE")
    coff = e_lfanew + 4
    (nsec,) = struct.unpack_from("<H", data, coff + 2)
    (optsize,) = struct.unpack_from("<H", data, coff + 16)
    opt = coff + 20
    (image_size,) = struct.unpack_from("<I", data, opt + 56)
    sect = opt + optsize
    out = []
    for i in range(nsec):
        off = sect + i * 40
        name = data[off:off + 8].rstrip(b"\0").decode("ascii", "replace")
        vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", data, off + 8)
        chars, = struct.unpack_from("<I", data, off + 36)
        out.append({"name": name, "vaddr": vaddr, "vsize": vsize,
                    "rawsize": rawsize, "rawptr": rawptr, "chars": chars})
    return data, out, image_size


def pdata_starts(data, sects):
    """-> (set of function begin RVAs, sorted list)."""
    pd = [s for s in sects if s["name"] == ".pdata"]
    if not pd:
        raise SystemExit("no .pdata")
    pd = pd[0]
    off = pd["rawptr"]
    n = pd["vsize"] // 12
    starts = set()
    for i in range(n):
        b, e, u = struct.unpack_from("<III", data, off + i * 12)
        if b:
            starts.add(b)
    return starts


def main():
    data, sects, image_size = sections_of(EXE)
    print("image size 0x%X" % image_size)
    for s in sects:
        print("  %-8s rva 0x%08X vsize 0x%08X raw 0x%08X  %s" %
              (s["name"], s["vaddr"], s["vsize"], s["rawsize"],
               "CODE" if s["chars"] & 0x20000000 else ("DATA" if s["chars"] & 0x80000000 else "")))
    starts = pdata_starts(data, sects)
    print(".pdata function starts: %d" % len(starts))

    text = [s for s in sects if s["name"] == ".text"][0]
    code_lo, code_hi = text["vaddr"], text["vaddr"] + text["vsize"]
    data_ranges = [(s["vaddr"], s["vaddr"] + s["vsize"]) for s in sects
                   if s["chars"] & 0xC0000000 == 0xC0000000 and s["name"] not in (".pdata",)]

    abs_lo = min(a for a, k in LOGGED.values())
    abs_hi = max(a for a, k in LOGGED.values())

    good = []
    # ASLR bases are 64K aligned.  The base must put every logged address inside
    # the image, so the search window is exactly bounded.
    for base in range((abs_hi - image_size) & ~0xFFFF, (abs_lo & ~0xFFFF) + 0x10000, 0x10000):
        if base <= 0:
            continue
        hits, miss = [], []
        for name, (a, kind) in LOGGED.items():
            rva = a - base
            if not (0 < rva < image_size):
                miss.append(name)
                break
            if kind == "func":
                if rva not in starts:
                    miss.append(name)
                    break
            else:
                if not any(lo <= rva < hi for lo, hi in data_ranges):
                    miss.append(name)
                    break
            hits.append(name)
        if len(hits) == len(LOGGED):
            good.append(base)

    print("\ncandidate bases satisfying ALL %d constraints: %d" % (len(LOGGED), len(good)))
    for base in good[:5]:
        print("\n*** base = 0x%016X ***" % base)
        for name, (a, kind) in sorted(LOGGED.items(), key=lambda kv: kv[1][0]):
            print("   %-26s 0x%016X  rva 0x%08X  %s" % (name, a, a - base, kind))
        # sanity: which section does each land in?
        for name, (a, kind) in sorted(LOGGED.items(), key=lambda kv: kv[1][0]):
            rva = a - base
            sec = "?"
            for s in sects:
                if s["vaddr"] <= rva < s["vaddr"] + s["vsize"]:
                    sec = s["name"]
                    break
            print("   %-26s -> %s" % (name, sec))
    if len(good) == 1:
        print("\nUNIQUE. This base and these RVAs are ground truth for this exe.")
    elif len(good) > 1:
        print("\nAMBIGUOUS -- need another constraint.")
    else:
        print("\nNO base satisfies every constraint; the logged set is inconsistent.")


if __name__ == "__main__":
    main()
