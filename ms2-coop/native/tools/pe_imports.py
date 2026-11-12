#!/usr/bin/env python3
"""Minimal PE reader used to plan the DollsMP native DLL.

Answers three questions, offline, with no third-party dependencies:

  1. Is the target packed?  (section entropy, import-table plausibility)
     If it is packed, an offline AOB scan is not viable and the address must
     be found at runtime instead.
  2. Which DLLs does it import, statically and by delay-load?
     A proxy DLL can only hijack a name the loader actually resolves, so the
     import list is the candidate set of proxy slots.
  3. Is the image safe to patch in-process?
     DllCharacteristics tells us ASLR / CFG; we need DYNAMIC_BASE to know the
     static RVAs stay RVAs, and Guard CF to know our detour trampoline may be
     checked.

Usage:  python tools/pe_imports.py <exe-or-dll> [--json out.json]
"""
import argparse
import json
import math
import struct
import sys

PE32 = 0x10B
PE32_PLUS = 0x20B

DIR_IMPORT = 1
DIR_TLS = 9
DIR_DELAY_IMPORT = 13

MACHINES = {
    0x014C: "x86",
    0x8664: "x64",
    0x01C0: "ARM",
    0xAA64: "ARM64",
}

SUBSYSTEMS = {
    1: "NATIVE",
    2: "WINDOWS_GUI",
    3: "WINDOWS_CUI",
}

# Characteristics bits we care about when judging patchability.
DLLCHAR = [
    (0x0020, "HIGH_ENTROPY_VA"),
    (0x0040, "DYNAMIC_BASE", "ASLR"),
    (0x0080, "FORCE_INTEGRITY"),
    (0x0100, "NX_COMPAT", "DEP"),
    (0x0200, "NO_ISOLATION"),
    (0x0400, "NO_SEH"),
    (0x0800, "NO_BIND"),
    (0x1000, "APPCONTAINER"),
    (0x2000, "WDM_DRIVER"),
    (0x4000, "GUARD_CF", "Control Flow Guard"),
    (0x8000, "TERMINAL_SERVER_AWARE"),
]

FILE_CHAR = [
    (0x0001, "RELOCS_STRIPPED"),
    (0x0002, "EXECUTABLE_IMAGE"),
    (0x2000, "DLL"),
    (0x0100, "32BIT_MACHINE"),
]

SCN_CHAR = {
    0x00000020: "CODE",
    0x00000040: "INITIALIZED_DATA",
    0x00000080: "UNINITIALIZED_DATA",
    0x20000000: "EXECUTE",
    0x40000000: "READ",
    0x80000000: "WRITE",
}


def entropy(data):
    """Shannon entropy in bits/byte. >7.2 over a whole section smells packed."""
    if not data:
        return 0.0
    counts = [0] * 256
    for b in data:
        counts[b] += 1
    n = float(len(data))
    ent = 0.0
    for c in counts:
        if c:
            p = c / n
            ent -= p * math.log(p, 2)
    return ent


class PE:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as fh:
            self.buf = fh.read()
        self.ok = False
        self.notes = []
        self._parse()

    def _u8(self, off):
        return self.buf[off]

    def _u16(self, off):
        return struct.unpack_from("<H", self.buf, off)[0]

    def _u32(self, off):
        return struct.unpack_from("<I", self.buf, off)[0]

    def _u64(self, off):
        return struct.unpack_from("<Q", self.buf, off)[0]

    def _parse(self):
        buf = self.buf
        if len(buf) < 0x40 or buf[:2] != b"MZ":
            self.notes.append("not a PE: missing MZ")
            return
        e_lfanew = self._u32(0x3C)
        if e_lfanew + 24 > len(buf) or buf[e_lfanew:e_lfanew + 4] != b"PE\0\0":
            self.notes.append("not a PE: missing PE signature")
            return
        coff = e_lfanew + 4
        self.machine = self._u16(coff)
        self.num_sections = self._u16(coff + 2)
        self.timestamp = self._u32(coff + 4)
        self.sym_ptr = self._u32(coff + 8)
        self.num_syms = self._u32(coff + 12)
        self.size_opt = self._u16(coff + 16)
        self.characteristics = self._u16(coff + 18)
        opt = coff + 20
        self.magic = self._u16(opt)
        if self.magic == PE32_PLUS:
            self.is64 = True
            self.image_base = self._u64(opt + 24)
            dd_off = opt + 112
            self.num_dd = self._u32(opt + 108)
        elif self.magic == PE32:
            self.is64 = False
            self.image_base = self._u32(opt + 28)
            dd_off = opt + 96
            self.num_dd = self._u32(opt + 92)
        else:
            self.notes.append("unknown optional-header magic 0x%X" % self.magic)
            return
        self.section_align = self._u32(opt + 32)
        self.file_align = self._u32(opt + 36)
        self.size_image = self._u32(opt + 56)
        self.size_headers = self._u32(opt + 60)
        self.subsystem = self._u16(opt + 68)
        self.dllchars = self._u16(opt + 70)
        self.entry_rva = self._u32(opt + 16)

        self.dd = []
        for i in range(min(self.num_dd, 16)):
            rva = self._u32(dd_off + i * 8)
            size = self._u32(dd_off + i * 8 + 4)
            self.dd.append((rva, size))

        # Sections
        self.sections = []
        s_off = opt + self.size_opt
        for i in range(self.num_sections):
            b = s_off + i * 40
            if b + 40 > len(buf):
                break
            name = buf[b:b + 8].rstrip(b"\0").decode("latin-1", "replace")
            vsize = self._u32(b + 8)
            vaddr = self._u32(b + 12)
            rawsize = self._u32(b + 16)
            rawptr = self._u32(b + 20)
            chars = self._u32(b + 36)
            raw = buf[rawptr:rawptr + rawsize] if rawsize else b""
            flags = [n for bit, n in SCN_CHAR.items() if chars & bit]
            self.sections.append({
                "name": name,
                "vaddr": vaddr,
                "vsize": vsize,
                "rawptr": rawptr,
                "rawsize": rawsize,
                "chars": "|".join(flags),
                "entropy": round(entropy(raw), 3) if raw else 0.0,
            })

        self.imports = self._read_imports()
        self.delay_imports = self._read_delay_imports()
        self.tls_callbacks = self._read_tls()
        self.ok = True

    def rva_to_off(self, rva):
        for s in self.sections:
            start = s["vaddr"]
            end = start + max(s["vsize"], s["rawsize"])
            if start <= rva < end:
                delta = rva - start
                if delta < s["rawsize"]:
                    return s["rawptr"] + delta
                return None
        if rva < self.size_headers:
            return rva
        return None

    def cstr(self, off, limit=256):
        if off is None or off >= len(self.buf):
            return None
        end = self.buf.find(b"\0", off, off + limit)
        if end < 0:
            return None
        return self.buf[off:end].decode("latin-1", "replace")

    def _thunk_names(self, rva, count_hint=None):
        """Walk an ILT/IAT thunk array; returns (names, has_ordinals)."""
        off = self.rva_to_off(rva)
        if off is None:
            return [], False
        names = []
        ordinal = False
        step = 8 if self.is64 else 4
        ordflag = 1 << (63 if self.is64 else 31)
        limit = 4096
        while limit:
            limit -= 1
            if off + step > len(self.buf):
                break
            val = self._u64(off) if self.is64 else self._u32(off)
            if val == 0:
                break
            if val & ordflag:
                ordinal = True
            else:
                hob = self.rva_to_off(val & 0x7FFFFFFF)
                if hob is not None and hob + 2 <= len(self.buf):
                    nm = self.cstr(hob + 2)
                    if nm:
                        names.append(nm)
            off += step
        return names, ordinal

    def _read_imports(self):
        if len(self.dd) <= DIR_IMPORT:
            return []
        rva, size = self.dd[DIR_IMPORT]
        if not rva:
            return []
        off = self.rva_to_off(rva)
        if off is None:
            return []
        out = []
        for i in range(512):
            b = off + i * 20
            if b + 20 > len(self.buf):
                break
            oft = self._u32(b)
            name_rva = self._u32(b + 12)
            first_thunk = self._u32(b + 16)
            if oft == 0 and name_rva == 0 and first_thunk == 0:
                break
            dll = self.cstr(self.rva_to_off(name_rva))
            names, ordinal = self._thunk_names(oft or first_thunk)
            out.append({
                "dll": dll,
                "functions": len(names),
                "by_ordinal": ordinal,
                "sample": sorted(names)[:40],
            })
        return out

    def _read_delay_imports(self):
        if len(self.dd) <= DIR_DELAY_IMPORT:
            return []
        rva, size = self.dd[DIR_DELAY_IMPORT]
        if not rva:
            return []
        off = self.rva_to_off(rva)
        if off is None:
            return []
        out = []
        for i in range(256):
            b = off + i * 32
            if b + 32 > len(self.buf):
                break
            attrs = self._u32(b)
            name_field = self._u32(b + 4)
            int_field = self._u32(b + 16)
            if not any(self._u32(b + k) for k in range(0, 32, 4)):
                break
            # If Attributes bit 0 is clear, the fields are true RVAs; if set,
            # they are VAs and must be rebased.
            if attrs & 1:
                base = self.image_base
                name_rva = name_field - base if name_field >= base else name_field
                int_rva = int_field - base if int_field >= base else int_field
            else:
                name_rva = name_field
                int_rva = int_field
            dll = self.cstr(self.rva_to_off(name_rva))
            names, ordinal = self._thunk_names(int_rva)
            out.append({
                "dll": dll,
                "attributes": attrs,
                "functions": len(names),
                "by_ordinal": ordinal,
                "sample": sorted(names)[:40],
            })
        return out

    def _read_tls(self):
        if len(self.dd) <= DIR_TLS:
            return []
        rva, size = self.dd[DIR_TLS]
        if not rva:
            return []
        off = self.rva_to_off(rva)
        if off is None or off + 40 > len(self.buf):
            return []
        if self.is64:
            cb = self._u64(off + 24)
            addr_off = off + 24
        else:
            cb = self._u32(off + 12)
            addr_off = off + 12
        cbs_va = self._u64(addr_off) if self.is64 else self._u32(addr_off)
        step = 8 if self.is64 else 4
        cbs_rva = cbs_va - self.image_base
        o = self.rva_to_off(cbs_rva)
        cbs = []
        if o is not None:
            for i in range(64):
                v = self._u64(o + i * step) if self.is64 else self._u32(o + i * step)
                if v == 0:
                    break
                cbs.append(v)
        return cbs


def known_dlls():
    """Read the authoritative KnownDLLs list from the registry.

    A proxy DLL is only honoured when the loader resolves that name from the
    application directory; a KnownDLL entry forces System32 and makes a local
    copy dead weight.  Returns None when the key cannot be read -- unknown is
    NOT the same as 'usable', and callers must not treat it as such.
    """
    try:
        import winreg
    except ImportError:
        return None
    try:
        key = winreg.OpenKey(
            winreg.HKEY_LOCAL_MACHINE,
            r"SYSTEM\CurrentControlSet\Control\Session Manager\KnownDLLs")
    except OSError:
        return None
    out = set()
    i = 0
    while True:
        try:
            _name, value, _kind = winreg.EnumValue(key, i)
        except OSError:
            break
        if isinstance(value, str):
            out.add(value.lower())
        i += 1
    return out or None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("target")
    ap.add_argument("--json")
    args = ap.parse_args()

    pe = PE(args.target)
    if not pe.ok:
        print("PARSE FAILED:", "; ".join(pe.notes))
        return 2

    print("== %s ==" % args.target)
    print("machine          : %s" % MACHINES.get(pe.machine, hex(pe.machine)))
    print("subsystem        : %s" % SUBSYSTEMS.get(pe.subsystem, pe.subsystem))
    print("image base       : 0x%X" % pe.image_base)
    print("size of image    : 0x%X" % pe.size_image)
    print("entry point RVA  : 0x%X" % pe.entry_rva)
    print("")
    fc = [n for bit, n in FILE_CHAR if pe.characteristics & bit]
    print("file chars       : %s" % ", ".join(fc))
    dc = []
    for entry in DLLCHAR:
        bit, name = entry[0], entry[1]
        if pe.dllchars & bit:
            dc.append(name + (" (%s)" % entry[2] if len(entry) > 2 else ""))
    print("dll chars        : %s" % (", ".join(dc) or "(none)"))
    print("")

    print("-- sections --")
    print("%-10s %-10s %-10s %-10s %-7s %s" % ("name", "vaddr", "vsize", "rawsize", "entropy", "chars"))
    for s in pe.sections:
        print("%-10s 0x%-8X 0x%-8X 0x%-8X %-7.3f %s" % (
            s["name"], s["vaddr"], s["vsize"], s["rawsize"], s["entropy"], s["chars"]))

    # Only CODE sections count towards the packing verdict. Resource and
    # relocation sections are legitimately near-random (compressed icons,
    # relocation deltas), so including them produces false "packed" verdicts --
    # this script did exactly that on the game exe before the heuristic was
    # narrowed to code-bearing sections.
    code_secs = [s for s in pe.sections
                 if ("CODE" in s["chars"] or "EXECUTE" in s["chars"]) and s["rawsize"] > 0x1000]
    high = [s for s in code_secs if s["entropy"] > 7.2]
    ignored = [s for s in pe.sections if s["entropy"] > 7.2 and s not in code_secs]
    print("")
    if not code_secs:
        print("packing verdict  : UNKNOWN - no code-bearing section to measure")
    elif high:
        print("packing verdict  : SUSPECT - code section(s) above 7.2 entropy: %s"
              % ", ".join(s["name"] for s in high))
    else:
        print("packing verdict  : UNPACKED - max code-section entropy %.3f (< 7.2)"
              % max(s["entropy"] for s in code_secs))
    if ignored:
        print("                   ignored high-entropy non-code: %s"
              % ", ".join(s["name"] for s in ignored))

    import_total = sum(i["functions"] for i in pe.imports)
    print("static imports   : %d DLLs, %d named functions" % (len(pe.imports), import_total))
    print("")
    for i in sorted(pe.imports, key=lambda d: (d["dll"] or "").lower()):
        ordn = " (+ordinals)" if i["by_ordinal"] else ""
        print("  %-24s %4d%s" % (i["dll"], i["functions"], ordn))

    if pe.delay_imports:
        print("")
        print("delay imports    : %d DLLs" % len(pe.delay_imports))
        for i in pe.delay_imports:
            print("  %-24s %4d" % (i["dll"], i["functions"]))

    print("")
    if pe.tls_callbacks:
        print("TLS callbacks    : %d -> %s" % (len(pe.tls_callbacks), [hex(v) for v in pe.tls_callbacks]))
    else:
        print("TLS callbacks    : none")

    # Proxy-slot analysis. Two independent conditions must hold, and a name
    # satisfying only one is useless:
    #   1. the loader must actually look for it, i.e. the image imports it
    #      (statically or by delay-load), and
    #   2. it must not be a KnownDLL, or System32 wins over the app directory.
    known = known_dlls()
    print("")
    if known is None:
        print("-- proxy-slot candidates --")
        print("   WARNING: KnownDLLs registry key unreadable. Cannot classify slots;")
        print("   every row below is UNKNOWN, not usable. Do not read this as a list.")
    else:
        print("-- proxy-slot candidates -- (%d KnownDLLs entries read from registry)" % len(known))

    def classify(name, kind):
        d = (name or "").lower()
        if known is None:
            verdict = "UNKNOWN (registry unreadable)"
        elif d in known:
            verdict = "DEAD - KnownDLL, local copy ignored"
        else:
            verdict = "USABLE"
        return verdict

    print("   %-24s %-10s %-34s %s" % ("dll", "kind", "verdict", "imports"))
    for i in sorted(pe.imports, key=lambda x: (x["dll"] or "").lower()):
        print("   %-24s %-10s %-34s %4d"
              % (i["dll"], "static", classify(i["dll"], "static"), i["functions"]))
    for i in sorted(pe.delay_imports, key=lambda x: (x["dll"] or "").lower()):
        print("   %-24s %-10s %-34s %4d"
              % (i["dll"], "delay", classify(i["dll"], "delay"), i["functions"]))
    print("")
    print("   NOTE: a delay-loaded name resolves only once something calls it, so a")
    print("   delay proxy's DllMain runs later than the exe entry point. A static")
    print("   import proxy is loaded by the loader before the entry point runs.")

    if args.json:
        with open(args.json, "w", encoding="utf-8") as fh:
            json.dump({
                "path": pe.path,
                "machine": MACHINES.get(pe.machine, hex(pe.machine)),
                "subsystem": SUBSYSTEMS.get(pe.subsystem, pe.subsystem),
                "image_base": pe.image_base,
                "size_image": pe.size_image,
                "entry_rva": pe.entry_rva,
                "dll_characteristics": pe.dllchars,
                "sections": pe.sections,
                "imports": pe.imports,
                "delay_imports": pe.delay_imports,
                "tls_callbacks": pe.tls_callbacks,
            }, fh, indent=2)
        print("\nwrote %s" % args.json)
    return 0


if __name__ == "__main__":
    sys.exit(main())
