// test_scan.cpp -- prove the DLL's AOB scanner finds the real game's globals.
//
// The scanner that ships is the one in ms2_ue.cpp.  This test feeds it the actual
// MortalShell2-Win64-Shipping.exe, laid out the way the loader would lay it out,
// and checks that all four patterns match exactly once and resolve to the RVAs
// that were recovered independently:
//
//   ProcessEvent  0x01619320   Dumper-7 Offsets block, and this repo's own
//                              pdata-based recovery (tools/recover_base.py)
//   AppendString  0x013F8FA0   Dumper-7 Offsets block
//   GObjects      0x0B03AEF0   Dumper-7 Offsets block (= GUObjectArray + 0x10,
//                              which UE4SS's log independently reported)
//   EngineTick    0x03DA7B30   UE4SS probe log + .pdata
//
// Nothing here runs the game.  A wrong pattern fails HERE, loudly, instead of
// quietly doing nothing in a live session.
//
//   test_scan.exe "E:\Games\_MS2-coop-test\...\MortalShell2-Win64-Shipping.exe"
#include <windows.h>
#include <stdio.h>
#include <cstdint>
#include <cstring>

#include "ms2_ue_test.h"

namespace {

struct Section { char name[9]; uint32_t vsize, vaddr, rawsize, rawptr; };

struct PeView {
    uint8_t* data = nullptr;
    size_t   size = 0;
    uint32_t imageSize = 0;
    uint32_t sizeOfHeaders = 0;
    Section  sections[32];
    int      count = 0;
};

bool ReadFileAll(const char* path, PeView& v) {
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER li;
    GetFileSizeEx(h, &li);
    v.size = (size_t)li.QuadPart;
    v.data = (uint8_t*)VirtualAlloc(nullptr, v.size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!v.data) { CloseHandle(h); return false; }
    DWORD got = 0;
    const BOOL ok = ReadFile(h, v.data, (DWORD)v.size, &got, NULL);
    CloseHandle(h);
    return ok && got == v.size;
}

bool ParsePe(PeView& v) {
    if (v.data[0] != 'M' || v.data[1] != 'Z') return false;
    const uint32_t e = *(uint32_t*)(v.data + 0x3C);
    const uint8_t* nt = v.data + e;
    if (nt[0] != 'P' || nt[1] != 'E') return false;
    const uint16_t nsec = *(uint16_t*)(nt + 6);
    const uint16_t optsize = *(uint16_t*)(nt + 20);
    const uint8_t* opt = nt + 24;
    v.imageSize = *(uint32_t*)(opt + 56);
    v.sizeOfHeaders = *(uint32_t*)(opt + 60);
    const uint8_t* s = opt + optsize;
    for (uint16_t i = 0; i < nsec && i < 32; ++i, s += 40) {
        Section sec;
        memset(&sec, 0, sizeof(sec));
        memcpy(sec.name, s, 8);
        sec.vsize = *(uint32_t*)(s + 8);
        sec.vaddr = *(uint32_t*)(s + 12);
        sec.rawsize = *(uint32_t*)(s + 16);
        sec.rawptr = *(uint32_t*)(s + 20);
        v.sections[v.count++] = sec;
    }
    return true;
}

// Lay the file out the way the loader does: headers at 0, each section at its RVA.
uint8_t* BuildImage(const PeView& v) {
    uint8_t* img = (uint8_t*)VirtualAlloc(nullptr, v.imageSize, MEM_COMMIT | MEM_RESERVE,
                                          PAGE_READWRITE);
    if (!img) return nullptr;
    memcpy(img, v.data, v.sizeOfHeaders);
    for (int i = 0; i < v.count; ++i) {
        const Section& s = v.sections[i];
        if (!s.vaddr || !s.rawsize) continue;
        if ((size_t)s.rawptr + s.rawsize > v.size) continue;
        memcpy(img + s.vaddr, v.data + s.rawptr, s.rawsize);
    }
    return img;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: test_scan.exe <MortalShell2-Win64-Shipping.exe>\n");
        return 2;
    }

    PeView v;
    if (!ReadFileAll(argv[1], v) || !ParsePe(v)) {
        printf("FAIL: could not read/parse %s\n", argv[1]);
        return 2;
    }
    printf("image: %s\n  size %.1f MB, %d sections\n", argv[1],
           (double)v.size / 1048576.0, v.count);
    for (int i = 0; i < v.count; ++i) {
        const Section& s = v.sections[i];
        if (!strcmp(s.name, ".text") || !strcmp(s.name, ".data"))
            printf("  %-8s rva 0x%08X vsize 0x%X\n", s.name, s.vaddr, s.vsize);
    }

    uint8_t* image = BuildImage(v);
    if (!image) { printf("FAIL: could not lay the image out\n"); return 2; }

    ue::detail::ScanCheck checks[8];
    const int n = ue::detail::RunPatternSelfCheck(image, checks, 8);
    if (n != 4) {
        printf("FAIL: the scanner ran %d of 4 patterns\n", n);
        return 1;
    }

    struct Expect { const char* name; uintptr_t rvaOld; uintptr_t rvaNew; };
    const Expect expect[] = {
        {"ProcessEvent", 0x01619320, 0x0161C420},
        {"AppendString", 0x013F8FA0, 0x013FC030},
        {"EngineTick",   0x03DA7B30, 0x03DEDE40},
        {"GObjects",     0x0B03AEF0, 0x0B25A5E0},
    };

    int bad = 0;
    for (int i = 0; i < 4; ++i) {
        const ue::detail::ScanCheck& c = checks[i];
        uintptr_t wantOld = 0, wantNew = 0;
        for (const Expect& e : expect) {
            if (!strcmp(e.name, c.name)) { wantOld = e.rvaOld; wantNew = e.rvaNew; }
        }

        const bool ok = (c.matches == 1 && (c.rva == wantOld || c.rva == wantNew));
        printf("  %-13s matches=%d  resolved=0x%08llX  expected=0x%08llX|0x%08llX  %s\n",
               c.name, c.matches,
               (unsigned long long)c.rva, (unsigned long long)wantOld, (unsigned long long)wantNew,
               ok ? "PASS" : (c.matches > 1 ? "FAIL (ambiguous)" : "FAIL"));
        if (!ok) ++bad;
    }

    if (bad) {
        printf("FAIL: %d pattern(s) did not resolve as expected -- do NOT ship this DLL\n", bad);
        return 1;
    }
    printf("PASS: all 4 patterns match exactly once and resolve to the recovered addresses\n");
    return 0;
}
