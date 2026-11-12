#!/usr/bin/env python3
"""Pick a UNIQUE wildcarded pattern for one function, checking as the DLL will.

The first attempt at AppendString's pattern wildcarded one rel32 by hand and
matched 27 places in .text -- caught by tests/test_scan.cpp, which runs the DLL's
own scanner.  So this tries increasing lengths and wildcards every displacement
that a relocation would move, and reports only the candidates that match exactly
once AND still resolve to the right address.

Usage: python pick_pattern.py AppendString 0x13F8FA0
"""
import struct
import sys

sys.path.insert(0, ".")
from recover_base import sections_of, EXE

# Opcode of the LAST byte before a rip-relative disp32 we want to wildcard.
RIP_OPCODES = {
    0x05: False, 0x0D: False, 0x15: False, 0x1D: False, 0x25: False, 0x2D: False,
    0x35: False, 0x3D: False, 0x3B: False, 0x39: False, 0x31: False, 0x29: False,
    0x8D: True, 0x8B: True, 0x89: True, 0x8F: True, 0x80: True, 0x83: True,
    0xFF: True, 0x0F: True, 0xE8: True, 0xE9: True, 0x74: True, 0x75: True,
}


def wildcard_mask(code, off, patlen, allow_disp_wildcards):
    mask = [0] * patlen
    if not allow_disp_wildcards:
        return mask
    for i in range(1, patlen - 4):
        b = code[off + i - 1]
        if code[off + i] not in (0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D,
                                 0x39, 0x31, 0x29, 0x3B) and code[off + i] not in (0x85, 0x8D):
            continue
        # a REX-prefixed rip-relative load/store: 48/4C 8B 3D .. or 80 3D .., 83 3D ..
        if b in (0x8B, 0x89, 0x8D, 0x8F, 0x80, 0x83, 0x3B, 0x39, 0x29, 0x31):
            for k in range(4):
                mask[i + k] = 1
    return mask


def count(code, off, raw, mask):
    n = len(raw)
    hits = 0
    pat = [i for i in range(n) if not mask[i]]
    if not pat:
        return 0
    needle = bytes([raw[i] for i in pat])
    start = 0
    while True:
        j = code.find(needle[:1], start)
        if j < 0:
            break
        start = j + 1
        o = j - pat[0]
        if o < 0 or o + n > len(code):
            continue
        ok = True
        for i in pat:
            if code[o + i] != raw[i]:
                ok = False
                break
        if ok:
            hits += 1
    return hits


def main():
    name = sys.argv[1] if len(sys.argv) > 1 else "AppendString"
    target = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x13F8FA0
    data, sects, image_size = sections_of(EXE)
    text = [s for s in sects if s["name"] == ".text"][0]
    code = data[text["rawptr"]: text["rawptr"] + text["rawsize"]]
    off = target - text["vaddr"]

    print("%s at exe+0x%X" % (name, target))
    for patlen in (24, 32, 40, 48, 56, 64):
        for wc in (True, False):
            if off + patlen > len(code):
                continue
            raw = code[off: off + patlen]
            mask = wildcard_mask(code, off, patlen, wc)
            n = count(code, off, raw, mask)
            aob = " ".join("??" if mask[i] else "%02X" % raw[i] for i in range(patlen))
            tag = "wildcarded" if any(mask) else "exact     "
            print("  len=%-2d %s matches=%d" % (patlen, tag, n))
            if n == 1:
                print("      \"%s\"" % aob)
                return 0
    print("  no unique candidate")


if __name__ == "__main__":
    sys.exit(main())
