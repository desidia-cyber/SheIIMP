// ms2_log.h -- the one log the native module writes.
//
// Deliberately tiny and allocation-free on the hot path: a line is formatted
// into a stack buffer and written with a single WriteFile under an SRW lock, so a
// log call from inside a ProcessEvent detour cannot allocate, cannot throw, and
// cannot deadlock against the loader (it takes no lock the loader owns).
//
// The file is `ms2coop-native.log` in the directory of the EXE -- not of the
// DLL -- because the DLL sits in Binaries\Win64 beside the exe, and "next to the
// exe" is what the deploy notes promise.
#pragma once

#include <cstddef>

namespace ms2 {

// Opens (or re-opens, truncating the previous run into .1.log) the log.
// Returns false if the file could not be created; logging then goes nowhere and
// the module keeps running -- a missing log must never be fatal.
bool OpenLog(const wchar_t* exeDir);
void CloseLog();
const wchar_t* LogPath();

// Appends one line (a trailing CRLF is added). Thread-safe. Truncates at 900 chars.
void Log(const char* line);

// printf-style into a stack buffer, then Log(). Supports %s %d %u %x %X %c %%
// %llu %lld %f and %p. Kept deliberately small: no printf dependency in a DLL
// that is loaded before the CRT of the game is initialised.
void Logf(const char* fmt, ...);

}  // namespace ms2
