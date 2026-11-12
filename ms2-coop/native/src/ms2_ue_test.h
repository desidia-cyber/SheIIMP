// ms2_ue_test.h -- test-only entry points into the reflection core.
//
// Deliberately separate from ms2_ue.h so the module's public surface stays the
// one the co-op code compiles against.  Used by tests/test_scan.cpp only.
#pragma once

#include <cstdint>

namespace ue {
namespace detail {

struct ScanCheck {
    const char* name = nullptr;
    uintptr_t   rva = 0;        // what the scanner resolved, as an RVA
    int         matches = 0;    // how many places in .text the pattern matched
};

// Runs the module's OWN AOB scanner over an in-memory image and reports what
// each built-in pattern resolves to, as an RVA.
//
// `imageBase` must point at a buffer that contains the image's PE headers at 0
// and its .text at the section's RVA -- i.e. a file image laid out the way the
// loader would.  Nothing here boots the game or touches a live process; it is how
// the patterns get proven against the real exe without running it.
//
// Returns the number of patterns checked (always 4 for now).
int RunPatternSelfCheck(const void* imageBase, ScanCheck* out, int max);

}  // namespace detail
}  // namespace ue
