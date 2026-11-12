#!/usr/bin/env python3
"""Pull the AOB (array-of-bytes) signature strings that UE4SS ships compiled in.

UE4SS 3.x embeds its pattern-search signatures as literal strings in UE4SS.dll.
This dumps every printable run that looks like one, plus the strings immediately
before/after it (the neighbours are usually the scan's own log text or a comment,
which identifies WHICH global the pattern is for).

Read-only: opens the DLL, writes a text report.
"""
import re
import sys

DLL = r"G:\Freespace\shell_coop\tools\ue4ss\dist\ue4ss\UE4SS.dll"

def runs(data, minlen=4):
    """Yield (offset, bytes) for printable ASCII runs."""
    pat = re.compile(rb"[\x20-\x7e]{%d,}" % minlen)
    for m in pat.finditer(data):
        yield m.start(), m.group(0)

def main():
    data = open(DLL, "rb").read()
    print("dll size", len(data))

    # A UE4SS pattern: hex byte pairs separated by single spaces, may contain '?'
    # e.g. "48 8B 05 ? ? ? ? 48 8B 88"
    hexpat = re.compile(rb"^(?:[0-9A-Fa-f]{2}|\?)(?: (?:[0-9A-Fa-f]{2}|\?))*$")
    # and the compact form without separators
    compact = re.compile(rb"^(?:[0-9A-Fa-f?]{2}){6,}$")

    hits = []
    for off, s in runs(data, 8):
        if b" " in s and hexpat.match(s) and s.count(b" ") >= 5:
            hits.append((off, s.decode("ascii")))
        elif compact.match(s) and len(s) >= 12:
            hits.append((off, s.decode("ascii")))

    print("pattern-like strings:", len(hits))
    with open(sys.argv[1] if len(sys.argv) > 1 else "ue4ss_sigs.txt", "w",
              encoding="utf-8") as fh:
        for off, s in hits:
            # context: nearest strings before and after
            before = []
            for o2, s2 in runs(data[:off][-4000:], 4):
                before.append(s2.decode("ascii", "replace"))
            after = []
            for o2, s2 in runs(data[off + len(s): off + len(s) + 4000], 4):
                after.append(s2.decode("ascii", "replace"))
            fh.write("=== 0x%X len=%d\n" % (off, len(s)))
            fh.write("  PAT  %s\n" % s)
            if before:
                fh.write("  prev  %s\n" % " | ".join(before[-3:]))
            if after:
                fh.write("  next  %s\n" % " | ".join(after[:3]))
    print("wrote report")

    # Also: any string mentioning the globals we care about, with neighbours.
    for kw in (b"GUObjectArray", b"FName::ToString", b"GNatives", b"GMalloc",
               b"ProcessEvent", b"GNamePool", b"FNamePool"):
        idxs = [m.start() for m in re.finditer(re.escape(kw), data)]
        print("%-16s %d occurrence(s) at %s" %
              (kw.decode(), len(idxs), ", ".join("0x%X" % i for i in idxs[:6])))

if __name__ == "__main__":
    main()
