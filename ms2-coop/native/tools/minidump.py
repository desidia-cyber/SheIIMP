#!/usr/bin/env python3
"""Minimal Windows minidump reader (no dependencies).

Enough of the format to (a) list modules and their load bases and (b) read any
byte of the captured address space through the Memory64ListStream.  That is what
lets us recover GROUND TRUTH for the UE5.6 globals: the dumps were taken from
real sessions of the same exe, so a global's RVA is its dump address minus the
dump's module base -- no guessing.
"""
import struct

MDMP_MAGIC = b"MDMP"
UNUSED = 0x00000000
MODULE_LIST = 4
MEMORY64_LIST = 9          # note: 9 in the spec, 6 is unused
MEMORY_LIST = 5


class Dump(object):
    def __init__(self, path):
        self.path = path
        self.data = open(path, "rb").read()
        if self.data[:4] != MDMP_MAGIC:
            raise SystemExit("%s is not a minidump" % path)
        (_sig, _ver, nstreams, dir_rva, _cksum, _ts, flags) = struct.unpack_from(
            "<4sIIIIIQ", self.data, 0)
        self.flags = flags
        self.streams = {}
        for i in range(nstreams):
            stype, size, rva = struct.unpack_from("<III", self.data, dir_rva + i * 12)
            self.streams.setdefault(stype, []).append((size, rva))
        self.modules = self._read_modules()
        self.ranges = self._read_memory()

    # -- streams -----------------------------------------------------------
    def _read_modules(self):
        out = []
        for size, rva in self.streams.get(MODULE_LIST, []):
            (nmod,) = struct.unpack_from("<I", self.data, rva)
            off = rva + 4
            for _ in range(nmod):
                (base, size_of_image, cksum, ts, name_rva) = struct.unpack_from(
                    "<QIIII", self.data, off)
                # MINIDUMP_MODULE is a fixed 0x6C: base(8) size(4) cksum(4) ts(4)
                # nameRva(4) VS_FIXEDFILEINFO(52) Cv(8) Misc(8) Reserved(16)
                off += 0x6C
                out.append({
                    "base": base,
                    "size": size_of_image,
                    "timestamp": ts,
                    "name": self._mdstring(name_rva).replace("\\", "/").split("/")[-1],
                })
        out.sort(key=lambda m: m["base"])
        return out

    def _mdstring(self, rva):
        (n,) = struct.unpack_from("<I", self.data, rva)
        return self.data[rva + 4: rva + 4 + n].decode("utf-16-le", "replace")

    def _read_memory(self):
        """-> sorted [(start, size, file_offset)] for 64- and 32-bit lists."""
        rngs = []
        for size, rva in self.streams.get(MEMORY64_LIST, []):
            (nranges, base_rva) = struct.unpack_from("<QQ", self.data, rva)
            off = rva + 16
            file_off = base_rva
            for _ in range(nranges):
                (start, dsize) = struct.unpack_from("<QQ", self.data, off)
                off += 16
                rngs.append((start, dsize, file_off))
                file_off += dsize
        for size, rva in self.streams.get(MEMORY_LIST, []):
            (nranges,) = struct.unpack_from("<I", self.data, rva)
            off = rva + 4
            for _ in range(nranges):
                (start, dsize, drva) = struct.unpack_from("<QII", self.data, off)
                off += 16
                rngs.append((start, dsize, drva))
        rngs.sort()
        return rngs

    # -- memory access -----------------------------------------------------
    def read(self, addr, size):
        lo, hi = 0, len(self.ranges)
        while lo < hi:                       # first range starting at/ before addr
            mid = (lo + hi) // 2
            if self.ranges[mid][0] <= addr:
                lo = mid + 1
            else:
                hi = mid
        i = lo - 1
        if i < 0:
            return None
        start, dsize, foff = self.ranges[i]
        if addr + size > start + dsize:
            return None
        o = foff + (addr - start)
        return self.data[o:o + size]

    def u64(self, addr):
        b = self.read(addr, 8)
        return None if b is None else struct.unpack("<Q", b)[0]

    def u32(self, addr):
        b = self.read(addr, 4)
        return None if b is None else struct.unpack("<I", b)[0]

    def i32(self, addr):
        b = self.read(addr, 4)
        return None if b is None else struct.unpack("<i", b)[0]

    def bytes_ok(self, addr, size=1):
        return self.read(addr, size) is not None

    def module_named(self, frag):
        for m in self.modules:
            if frag.lower() in m["name"].lower():
                return m
        return None

    def summary(self):
        print("file          : %s (%.1f MB)" % (self.path, len(self.data) / 1048576.0))
        print("modules       : %d" % len(self.modules))
        print("memory ranges : %d" % len(self.ranges))
        if self.ranges:
            lo = self.ranges[0][0]
            hi = max(r[0] + r[1] for r in self.ranges)
            print("address space : 0x%X .. 0x%X" % (lo, hi))


if __name__ == "__main__":
    import sys
    d = Dump(sys.argv[1])
    d.summary()
    for m in d.modules:
        print("  %-40s base=0x%016X size=0x%X ts=0x%08X" %
              (m["name"], m["base"], m["size"], m["timestamp"]))
