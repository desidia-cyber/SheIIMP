#!/usr/bin/env python3
"""Build AOB patterns that are CHECKED against the shipping image.

A pattern is only useful if it resolves, on this binary, to exactly the address we
derived independently (recover_base.py + the Dumper-7 Offsets block).  So each
candidate here is emitted as an AOB string with '??' over the bytes that vary
(a rel32 displacement, an immediate) and then evaluated the way the DLL's scanner
will evaluate it: scan the whole .text, count matches, and resolve the relative
operand.  Candidates that match more than once, or resolve elsewhere, are dropped
and reported as dropped -- never silently shipped.

Output is C++-ready: a table of { name, aob, resolve-kind }.
"""
import struct
import sys

import capstone

sys.path.insert(0, ".")
from recover_base import sections_of, pdata_starts, EXE

IMAGE_BASE = 0x140000000

# Targets and how the DLL gets an address out of a match.
#   "abs"  -> the match address IS the answer (function entry)
#   "rip"  -> the match contains a rip-relative displacement at disp_offset,
#             and the instruction containing it ends at disp_offset+4
TARGETS = [
    ("ProcessEvent",      0x01619320, "abs"),
    ("AppendString",      0x013F8FA0, "abs"),
    ("GameEngineTick",    0x03DA7B30, "abs"),
    ("GObjects",          0x0B03AEF0, "rip"),
    ("FName::ToString",   0x01405610, "abs"),
]


def load():
    data, sects, image_size = sections_of(EXE)
    text = [s for s in sects if s["name"] == ".text"][0]
    code = data[text["rawptr"]: text["rawptr"] + text["rawsize"]]
    return data, sects, text, code


def match_at(code, base_rva, target, kind, patlen):
    """Emit candidate (start_rva, aob_bytes, mask_bits, disp_off) tuples."""
    out = []
    if kind == "abs":
        starts = [target]
    else:
        # every properly aligned '48 8B 05/0D/15/1D/35/3D disp32' referring to target
        starts = []
        for op in (0x05, 0x0D, 0x15, 0x1D, 0x35, 0x3D, 0x3B):
            i = 0
            while True:
                j = code.find(bytes([0x48, 0x8B, op]), i)
                if j < 0:
                    break
                i = j + 1
                disp, = struct.unpack_from("<i", code, j + 3)
                if base_rva + j + 7 + disp == target:
                    starts.append(base_rva + j)
    for s in starts:
        off = s - base_rva
        if off < 0 or off + patlen > len(code):
            continue
        raw = code[off: off + patlen]
        mask = [0] * patlen
        disp_off = -1
        if kind == "rip":
            # wildcard the rel32 right after the 48 8B xx opcode
            disp_off = 3
            for k in range(4):
                mask[disp_off + k] = 1
        else:
            # wildcard immediates / displacements conservatively: any disp32 that
            # follows a rip-relative opcode inside the window
            for k in range(patlen - 4):
                if code[off + k] in (0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D) and k >= 1:
                    if code[off + k - 1] in (0x8B, 0x8D, 0x89, 0x8F, 0xE8, 0xE9, 0x70, 0x74, 0x75, 0xEB, 0x0F):
                        for m in range(4):
                            mask[k + m] = 1
        out.append((s, raw, mask, disp_off))
    return out


def aob_string(raw, mask):
    return " ".join("??" if mask[i] else "%02X" % raw[i] for i in range(len(raw)))


def count_matches(code, base_rva, raw, mask):
    """-> list of match RVAs, using the same masked compare the DLL will use."""
    n = len(raw)
    first = None
    pat = []
    for i in range(n):
        if not mask[i]:
            pat.append(i)
    if not pat:
        return []
    needle = bytes([raw[i] for i in pat])
    hits = []
    start = 0
    while True:
        j = code.find(needle[:1], start)
        if j < 0:
            break
        start = j + 1
        off = j - pat[0]
        if off < 0 or off + n > len(code):
            continue
        ok = True
        for i in pat:
            if code[off + i] != raw[i]:
                ok = False
                break
        if ok:
            hits.append(base_rva + off)
    return hits


def main():
    data, sects, text, code = load()
    lo = text["vaddr"]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

    print("AOB patterns verified against %s" % EXE)
    for name, target, kind in TARGETS:
        print("\n=== %s  (exe+0x%X, %s)" % (name, target, kind))
        for patlen in (24, 32, 40, 56):
            for (s, raw, mask, disp_off) in match_at(code, lo, target, kind, patlen):
                aob = aob_string(raw, mask)
                hits = count_matches(code, lo, raw, mask)
                if len(hits) != 1:
                    continue
                # resolve the way the DLL will
                if kind == "rip":
                    disp, = struct.unpack_from("<i", code, s - lo + disp_off)
                    resolved = s + disp_off + 4 + disp
                else:
                    resolved = hits[0]
                if resolved != target:
                    print("   len %d @exe+0x%X resolves to 0x%X (WRONG) -- dropped"
                          % (patlen, s, resolved))
                    continue
                ins = "?"
                for i in md.disasm(code[s - lo: s - lo + 16], s):
                    ins = "%s %s" % (i.mnemonic, i.op_str)
                    break
                print("   OK len=%d @exe+0x%X unique, resolves 0x%X   [%s]"
                      % (patlen, s, resolved, ins))
                print('      "%s"' % aob)
                break
            else:
                continue
            break
        else:
            print("   no unique candidate found")


if __name__ == "__main__":
    main()
