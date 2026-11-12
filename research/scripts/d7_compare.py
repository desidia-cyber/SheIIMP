#!/usr/bin/env python3
"""Compare the Dumper-7 SDK against the UE4SS CXX header dump.

Both generators read the same reflection data.  For every class that exists in
both outputs this script compares:

  * the parent class name
  * every property slot (offset -> name).  UE4SS prints the raw FName and
    Dumper-7 the sanitised C++ identifier, and both render a bitfield group
    differently, so a slot is matched by OFFSET; a differing name is reported as
    a rendering difference, never as a layout disagreement.
  * the total class size (UE4SS "// Size: 0xNNNN" against Dumper-7
    "// 0xOWN (0xTOTAL - 0xBASE)"; Dumper-7 rounds up to the class alignment).

usage:
  python d7_compare.py --d7 <CppSDK\\SDK> --ue4ss <raw\\CXXHeaderDump>
         [--json out.json] [--sample N] [--key "UClass,UClass2"]
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

BASE = r"[A-Za-z_][\w:]*(?:<[^>]*>)?"

CLASS_START_UE4SS = re.compile(r"^class\s+([A-Za-z0-9_]+)\s*(?::\s*public\s+(" + BASE + r")(?:\s*,\s*public\s+" + BASE + r")*)?\s*$")
CLASS_SIZE_UE4SS = re.compile(r"^};\s*//\s*Size:\s*0x([0-9A-Fa-f]+)")
MEMBER_LINE = re.compile(r"^(.*?);\s*//\s*0x([0-9A-Fa-f]+)\s*(?:\(size:\s*0x([0-9A-Fa-f]+)\))?")

CLASS_START_D7 = re.compile(r"^class\s+(?:(?:SDK_ALIGN|alignas)\(0x[0-9A-Fa-f]+\)\s+)?([A-Za-z0-9_]+)(?:\s+final)?\s*(?::\s*public\s+(" + BASE + r")(?:\s*,\s*public\s+" + BASE + r")*)?\s*$")
CLASS_SIZE_D7 = re.compile(r"^//\s*0x([0-9A-Fa-f]+)\s*\(0x([0-9A-Fa-f]+)\s*-\s*0x([0-9A-Fa-f]+)\)")
MEMBER_D7 = re.compile(
    r"^[^\S\n]*([A-Za-z_][\w:<>,\s\*&()]*?)\s+([A-Za-z_]\w*)"
    r"\s*(?:\[[^\]]+\])?\s*(?::\s*\d+)?\s*;\s*//\s*0x([0-9A-Fa-f]+)(?::[\dA-Fa-f]+)?\(0x?([0-9A-Fa-f]+)\)"
)
BITINDEX_D7 = re.compile(r"BitIndex:\s*0x([0-9A-Fa-f]+)")

SKIP_PREFIX = ("//", "#", "/*", "*", "public:", "private:", "protected:", "SDK_NAMESPACE")


def norm(name: str) -> str:
    return re.sub(r"\s+", "_", name.strip())


def split_decl(decl: str) -> str | None:
    """'class USpartaWeatherPreset* Weather Preset' -> 'Weather Preset'.

    The property name may contain spaces/parentheses (UE4SS prints the raw
    FName), so the type is consumed first: qualifier keywords, one identifier
    with optional template arguments and optional '*'/'&'.
    """
    s = decl.strip().rstrip("*&").strip()
    if not s:
        return None
    while True:
        m = re.match(r"^(class|struct|const|enum|mutable|typename|volatile)\s+(.*)$", s)
        if not m:
            break
        s = m.group(2).strip()
    m = re.match(r"^[A-Za-z_]\w*(?:::\w+)*\s*(<.*>|\(.*\))?\s*([\*&]+)?\s*(.*)$", s, re.S)
    if not m:
        return None
    rest = m.group(3).strip()
    if not rest:
        return None
    rest = re.sub(r"\[[^\]]*\]$", "", rest).strip()
    rest = re.split(r"\s*:\s*", rest)[0].strip()
    return rest or None


class Cls:
    __slots__ = ("name", "parent", "size", "members", "bits", "file")

    def __init__(self, name: str, file: str):
        self.name = name
        self.parent = None
        self.size = None
        self.members: dict[str, int] = {}
        self.bits: dict[str, int] = {}
        self.file = file

    def __repr__(self) -> str:
        return f"<{self.name} size={self.size} members={len(self.members)}>"


def parse_ue4ss(root: Path) -> dict[str, Cls]:
    out: dict[str, Cls] = {}
    for path in sorted(root.glob("*.hpp")):
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        cur: Cls | None = None
        for line in text.splitlines():
            if line.startswith("class "):
                m = CLASS_START_UE4SS.match(line.strip())
                if m:
                    cur = Cls(m.group(1), path.name)
                    cur.parent = m.group(2)
                    out.setdefault(cur.name, cur)
                continue
            if cur is None:
                continue
            s = line.strip()
            m = CLASS_SIZE_UE4SS.match(s)
            if m:
                cur.size = int(m.group(1), 16)
                cur = None
                continue
            if s.startswith("};"):
                cur = None
                continue
            if not s or s.startswith(SKIP_PREFIX):
                continue
            m = MEMBER_LINE.match(s)
            if m:
                name = split_decl(m.group(1))
                if name:
                    cur.members.setdefault(norm(name), int(m.group(2), 16))
    return out


def parse_d7(root: Path) -> dict[str, Cls]:
    out: dict[str, Cls] = {}
    for path in sorted(root.glob("*_classes.hpp")):
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        cur: Cls | None = None
        pending: tuple[int, int, int] | None = None
        for line in text.splitlines():
            s = line.strip()
            m = CLASS_SIZE_D7.match(s)
            if m:
                pending = (int(m.group(1), 16), int(m.group(2), 16), int(m.group(3), 16))
                continue
            m = CLASS_START_D7.match(s)
            if m:
                cur = Cls(m.group(1), path.name)
                cur.parent = m.group(2)
                if pending:
                    cur.size = pending[1]           # 2nd number = total size incl. bases
                    pending = None
                out.setdefault(cur.name, cur)
                continue
            if cur is None:
                continue
            if s.startswith("};"):
                cur = None
                continue
            if not s or s.startswith(SKIP_PREFIX):
                continue
            m = MEMBER_D7.match(line)
            if m:
                name = m.group(2)
                if name.startswith("Pad_") or name == "VTable":
                    continue
                cur.members.setdefault(name, int(m.group(3), 16))
                bm = BITINDEX_D7.search(line)
                if bm:
                    cur.bits[name] = int(bm.group(1), 16)
    return out


def norm_parent(name: str | None) -> str | None:
    """Dumper-7 qualifies a collided base class as 'Namespace::Class'; keep the bare name."""
    if not name:
        return name
    return name.split("::")[-1]


def is_interface(c: Cls) -> bool:
    return c.name.startswith("I") and c.name[1:2].isupper() and (
        c.parent is None or c.parent == "IInterface" or c.name.endswith("Interface"))


def aligned_ok(ue4_size: int, d7_size: int) -> bool:
    if d7_size <= ue4_size:
        return False
    for align in (8, 16):
        if d7_size % align == 0 and (ue4_size + align - 1) // align * align == d7_size:
            return True
    return False


def rendering_kind(d7_name: str, ue4_name: str) -> str:
    if d7_name.startswith("UberGraphFrame") and ue4_name == "UberGraphFrame":
        return "member-alias-frame (UE4SS prints 'UberGraphFrame')"
    if "(" in ue4_name or ")" in ue4_name:
        return "parentheses/space in FName"
    if ue4_name in d7_name or d7_name.rstrip("_") == ue4_name:
        return "trailing '_' disambiguation"
    if re.sub(r"_\d+$", "", d7_name) == ue4_name:
        return "numeric suffix disambiguation"
    return "other"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--d7", required=True, help="Dumper-7 CppSDK\\SDK directory")
    ap.add_argument("--ue4ss", required=True, help="UE4SS CXXHeaderDump directory")
    ap.add_argument("--json", default=None)
    ap.add_argument("--sample", type=int, default=12, help="rows printed per category")
    ap.add_argument("--key", default="", help="comma separated class names for a short table")
    args = ap.parse_args()

    ue4 = parse_ue4ss(Path(args.ue4ss))
    d7 = parse_d7(Path(args.d7))
    both = sorted(set(ue4) & set(d7))
    only_ue4 = sorted(set(ue4) - set(d7))
    only_d7 = sorted(set(d7) - set(ue4))

    size_diff, size_aligned, size_missing, size_interface = [], [], [], []
    parent_diff, name_diff, slot_only_d7, slot_only_ue4 = [], [], [], []
    kinds: dict[str, int] = {}
    member_cmp = bitfields = 0

    for name in both:
        a, b = ue4[name], d7[name]
        if a.size is None or b.size is None:
            size_missing.append((name, a.size, b.size))
        elif a.size != b.size:
            row = (name, a.size, b.size)
            if is_interface(b) or (b.size == 8 and not b.members):
                size_interface.append(row)      # interface: Dumper-7 0x8 vs UE4SS 0x28
            else:
                (size_aligned if aligned_ok(a.size, b.size) else size_diff).append(row)
        if norm_parent(a.parent) != norm_parent(b.parent) and not is_interface(b):
            parent_diff.append((name, a.parent, b.parent))

        ue4_slots = {off: prop for prop, off in a.members.items()}
        ue4_offsets = set(ue4_slots)
        d7_offsets = {off for prop, off in b.members.items() if prop not in b.bits}
        for prop, off in b.members.items():
            if prop in b.bits:
                bitfields += 1
                continue
            member_cmp += 1
            if off in ue4_offsets:
                other = ue4_slots[off]
                if other != prop:
                    kind = rendering_kind(prop, other)
                    kinds[kind] = kinds.get(kind, 0) + 1
                    name_diff.append((name, off, prop, other))
            else:
                slot_only_d7.append((name, prop, off))
        for off in sorted(ue4_offsets - d7_offsets):
            slot_only_ue4.append((name, ue4_slots[off], off))

    res = {
        "ue4ss_classes": len(ue4), "d7_classes": len(d7),
        "both": len(both), "only_ue4ss": only_ue4, "only_d7": only_d7,
        "members_compared": member_cmp, "bitfield_members": bitfields,
        "parent_mismatch": parent_diff,
        "size_mismatch_real": size_diff,
        "size_mismatch_alignment_rounding": size_aligned,
        "size_interface_render": size_interface,
        "size_unparsed": size_missing,
        "name_rendering_diff": name_diff,
        "name_rendering_kinds": kinds,
        "slot_only_in_d7": slot_only_d7,
        "slot_only_in_ue4ss": slot_only_ue4,
    }

    print(f"ue4ss classes {len(ue4)} | dumper-7 classes {len(d7)} | shared {len(both)}")
    print(f"only ue4ss {len(only_ue4)} | only dumper-7 {len(only_d7)}")
    print(f"property slots compared {member_cmp} | bitfield members (separate) {bitfields}")
    print(f"parent mismatches {len(parent_diff)}")
    print(f"class size: real {len(size_diff)} | alignment rounding {len(size_aligned)} | interface 0x8 {len(size_interface)}")
    print(f"slot only in dumper-7 {len(slot_only_d7)} | slot only in ue4ss {len(slot_only_ue4)}")
    print(f"same slot, different rendered name {len(name_diff)}: {kinds}")
    for label, rows in (("SIZE-REAL", size_diff), ("PARENT", parent_diff),
                        ("SLOT-ONLY-D7", slot_only_d7), ("SLOT-ONLY-UE4SS", slot_only_ue4),
                        ("NAME", name_diff)):
        for row in rows[:args.sample]:
            print(f"  {label} {row}")
    if args.key:
        print("key classes: ue4ss size -> d7 size | ue4ss slots -> d7 slots | d7 Pad_ entries")
        for cls in [c.strip() for c in args.key.split(",") if c.strip()]:
            if cls not in both:
                print(f"  {cls:40s} absent from one side")
                continue
            a, b = ue4[cls], d7[cls]
            pad = 0
            hdr = (Path(args.d7) / b.file)
            if hdr.is_file():
                pad = sum(1 for line in hdr.read_text(encoding="utf-8", errors="replace").splitlines()
                          if re.search(r"\bPad_[0-9A-F]+\[", line))
            print(f"  {cls:40s} 0x{(a.size or 0):04X} -> 0x{(b.size or 0):04X}  "
                  f"{len(a.members):3d} -> {len(b.members):3d}  pad {pad}")
    if args.json:
        Path(args.json).write_text(json.dumps(res, indent=1), encoding="utf-8")
        print(f"json -> {args.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

