#!/usr/bin/env python3
"""Byte-level RIP-relative reference finder, vectorised.

Scanning every .pdata function with capstone is far too slow for a 135 MB .text.
But a RIP-relative reference is mechanical: at some byte offset i a 4-byte
little-endian displacement d encodes the target (i + 4) + d.  So for each target
T the required d at offset i is exactly T - (i + 4) -- one subtraction per byte,
which numpy does in chunks.  Anything that comes back is then confirmed with
capstone on a real instruction boundary around it.

That gives every code site that touches a global, from which the AOB patterns
below are taken.
"""
import struct
import sys

import numpy as np
import capstone

sys.path.insert(0, ".")
from recover_base import sections_of, pdata_starts, EXE

TARGETS = {
    "GUObjectArray":   0x0B03AEE0,
    "ObjObjects":      0x0B03AEF0,
    "ProcessEvent":    0x01619320,
    "AppendString":    0x013F8FA0,
    "FName::ToString": 0x01405610,
    "GNatives":        0x0B039970,
    "GMalloc":         0x0AF4B3C0,
    "GWorld":          0x0AD4E7F0,
}

CHUNK = 1 << 24


def find_refs(code, base_rva, targets):
    """-> {name: [ (position_of_disp32_in_file, instruction_rva_guess) ]}"""
    out = {k: [] for k in targets}
    n = len(code)
    # 4-byte reads at every byte offset
    for start in range(0, n - 4, CHUNK):
        end = min(start + CHUNK, n - 4)
        block = code[start:end + 4]
        arr = np.frombuffer(block, dtype=np.uint8)
        # little-endian int32 at every offset
        i32 = (arr[:-3].astype(np.uint32)
               | (arr[1:-2].astype(np.uint32) << 8)
               | (arr[2:-1].astype(np.uint32) << 16)
               | (arr[3:].astype(np.uint32) << 24)).view(np.int32)
        idx = np.arange(start, start + (end - start) + 1, dtype=np.int64)
        for name, t in targets.items():
            # instruction ends at idx+4, so RIP = base_rva + idx + 4
            need = (t - (base_rva + idx + 4)).astype(np.int64)
            need32 = need.astype(np.int32)
            ok = (need32.astype(np.int64) == need) & (i32 == need32)
            where = np.nonzero(ok)[0]
            for w in where:
                out[name].append(int(idx[w]))
    return out


def main():
    data, sects, image_size = sections_of(EXE)
    starts = pdata_starts(data, sects)
    text = [s for s in sects if s["name"] == ".text"][0]
    lo = text["vaddr"]
    code = data[text["rawptr"]: text["rawptr"] + text["rawsize"]]

    print("scanning 0x%X bytes of .text at rva 0x%X" % (len(code), lo))
    refs = find_refs(code, lo, TARGETS)

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True

    for name in sorted(refs):
        lst = refs[name]
        print("\n== %-16s %d candidate disp32 site(s)" % (name, len(lst)))
        shown = 0
        for pos in lst:
            # find an instruction boundary that ends at pos+4 by trying the usual
            # x86-64 lengths, then confirm with capstone
            found = None
            for back in range(2, 12):
                s = pos - back
                if s < 0:
                    continue
                chunk = code[s: s + back + 4 + 8]
                for ins in md.disasm(chunk, lo + s):
                    if ins.address + ins.size == lo + pos + 4:
                        found = ins
                        break
                    break        # only accept the first decode at this start
                if found:
                    break
            if not found:
                continue
            ins = found
            raw = code[(ins.address - lo): (ins.address - lo) + ins.size + 8]
            print("    exe+0x%07X  %-24s %s %s   | %s" %
                  (ins.address, ins.bytes.hex().upper(), ins.mnemonic, ins.op_str,
                   " ".join("%02X" % c for c in raw)))
            shown += 1
            if shown >= 10:
                print("    ... (%d more)" % (len(lst) - shown))
                break


if __name__ == "__main__":
    main()
