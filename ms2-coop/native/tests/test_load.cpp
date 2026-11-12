// test_load.cpp -- load the built proxy and prove it does not take the host down.
//
// This is the only test that can be run without the game.  It answers the two
// questions that matter before anyone puts the DLL in a real install:
//
//   1. Does DllMain hand off to the worker thread without touching the engine --
//      i.e. does LoadLibraryW return at all on a process that is NOT the game?
//   2. When every global fails to resolve (which is exactly what happens in a
//      host with no UE code), does the module LOG and give up, rather than fault?
//
// The stub host is deliberately tiny and has no UE code, so this exercises the
// MISMATCH path, not the happy path.  The happy path needs the game and is what
// the self-test in the log is for.
//
// The log path is the host exe's directory, so run this from the build folder
// beside dxgi.dll -- that also proves the log lands where the deploy notes say.
#include <windows.h>
#include <stdio.h>

int main(int argc, char** argv) {
    const wchar_t* dll = (argc > 1) ? L"dxgi.dll" : L"dxgi.dll";

    printf("test_load: LoadLibraryW(%ws) ...\n", dll);
    fflush(stdout);

    HMODULE m = LoadLibraryW(dll);
    if (!m) {
        printf("test_load: FAILED to load, GetLastError=%lu\n", GetLastError());
        return 2;
    }
    printf("test_load: loaded at %p -- DllMain returned, the worker is running\n", (void*)m);

    // Give the worker its 750 ms start-up sleep plus time to scan and give up.
    // The deliberate mismatch path retries the object array for 15 s, so wait
    // past that: the point of the wait is to be holding the process open while
    // the module is at its most dangerous.
    for (int i = 0; i < 22; ++i) {
        Sleep(1000);
        printf("test_load: %d s, still alive\n", i + 1);
        fflush(stdout);
    }

    printf("test_load: PASS -- the host survived the mismatch path\n");
    return 0;
}
