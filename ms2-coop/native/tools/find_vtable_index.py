#!/usr/bin/env python3
"""Derive, from the shipping image itself, the two things a UE5.6 reflection
core must not guess:

  1. ProcessEvent's UObject VTABLE INDEX.  Every UObject-derived vtable carries
     ProcessEvent at the same slot, so the pointer value identifies the slot --
     but the slot NUMBER depends on where the table starts, which is what this
     prints, with .pdata function starts used to tell code pointers from
     neighbouring data.
  2. The UObject member offsets, by reading the exe's own .data for the objects
     Dumper-7 left there... (not applicable) -- instead the offsets come from the
     Dumper-7 SDK header and are cross-checked against the object-array walk in
     find_globals.py.

RVAs here are the ones recover_base.py proved unique for this build.
"""
import struct
import sys

sys.path.insert(0, ".")
from recover_base import sections_of, pdata_starts, EXE

RVA = {
    "ProcessEvent": 0x01619320,
    "ProcessInternal": 0x01619710,
    "ProcessLocalScriptFunction": 0x01619810,
    "FName::ToString": 0x01405610,
    "GUObjectArray": 0x0B03AEE0,
    "GNatives": 0x0B039970,
    "GMalloc": 0x0AF4B3C0,
}
IMAGE_BASE = 0x140000000


def main():
    data, sects, image_size = sections_of(EXE)
    starts = pdata_starts(data, sects)
    text = [s for s in sects if s["name"] == ".text"][0]
    code_lo, code_hi = text["vaddr"], text["vaddr"] + text["vsize"]
    rd = [s for s in sects if s["name"] == ".rdata"][0]

    for key in ("ProcessEvent",):
        want = IMAGE_BASE + RVA[key]
        needle = struct.pack("<Q", want)
        occ = []
        pos = data.find(needle, rd["rawptr"], rd["rawptr"] + rd["rawsize"])
        while pos != -1:
            occ.append(pos)
            pos = data.find(needle, pos + 1, rd["rawptr"] + rd["rawsize"])
        print("%s (rva 0x%X): %d pointer occurrence(s) in .rdata" % (key, RVA[key], len(occ)))

        for pos in occ[:6]:
            rva_here = rd["vaddr"] + (pos - rd["rawptr"])
            print("\n  occurrence at .rdata rva 0x%X" % rva_here)
            # walk backwards over plausible code pointers to find a table start
            p = pos
            run = 0
            while p - 8 >= rd["rawptr"]:
                v, = struct.unpack_from("<Q", data, p - 8)
                if IMAGE_BASE <= v < IMAGE_BASE + image_size:
                    r = v - IMAGE_BASE
                    if code_lo <= r < code_hi:
                        p -= 8
                        run += 1
                        continue
                break
            start_rva = rd["vaddr"] + (p - rd["rawptr"])
            print("     backwards-run start at rva 0x%X -> index would be 0x%X (%d)"
                  % (start_rva, run, run))
            # print the table with slot numbers, relative to the run start
            for i in range(-2, 8):
                a = pos + i * 8
                if a < rd["rawptr"] or a + 8 > rd["rawptr"] + rd["rawsize"]:
                    continue
                v, = struct.unpack_from("<Q", data, a)
                slot = i + run
                if IMAGE_BASE <= v < IMAGE_BASE + image_size:
                    r = v - IMAGE_BASE
                    mark = "FUNC" if r in starts else ("code?" if code_lo <= r < code_hi else "")
                    print("     slot 0x%03X (%+d): 0x%X %s" % (slot, i, r, mark))
                else:
                    print("     slot 0x%03X (%+d): raw 0x%X  <-- not a pointer"
                          % (slot, i, v))


if __name__ == "__main__":
    main()
