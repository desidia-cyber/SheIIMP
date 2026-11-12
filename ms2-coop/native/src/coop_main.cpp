// coop_main.cpp -- the co-op module's entry point.
//
// The proxy DLL (dllmain / its worker thread) resolves the engine layer first and
// calls coop::Start() **only** after ue::Init() has succeeded.  Everything that
// has to happen once, in order, happens here:
//
//   1. the engine layer is verified ready (no engine calls before this point --
//      ms2_ue.cpp: "Nothing runs at DllMain time");
//   2. the base layer comes up: coop dir (coop_dir.txt), ME / ROLE, the kill
//      switches read once, stale out_/peer_ files removed, the
//      "=== CoopMod start ===" banner and result.txt = RUNNING (coop_net's
//      net::Start(), which is main.lua:24-153 + 4129-4133);
//   3. the single per-frame tick is registered with the engine layer.  Every
//      module's work hangs off it through coop_net's 30/15/2/1 Hz router
//      (coop::Tick), which is the replacement for the Lua's four LoopAsync
//      timers;
//   4. nothing else.  There is no hook to install (main.lua has zero
//      RegisterHook call sites) and no delegate to bind: everything is polled,
//      exactly like the Lua.
//
// Failure policy: this function must never throw into the loader.  A co-op module
// that cannot start has to leave the game running normally, so every step is
// guarded and a failure is logged rather than propagated.

#include "coop_state.h"

#include "ms2_log.h"
#include "ms2_ue.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace coop {

namespace {

bool g_started = false;
bool g_tickRegistered = false;

// The engine layer's per-frame callback (ue::OnTick): the game thread, once per
// frame, with the engine delta.  net::Frame is the router; it also owns the
// world-change detection and the settle-window bookkeeping that used to live in
// the Lua's step().
void OnEngineTick(float dt) {
    try {
        if (!ue::IsReady()) return;
        net::Frame((double)dt);
    } catch (...) {
        // A throw out of a detour would take the frame down with it.  Say so once
        // per event; the loop keeps its own state and tries again next frame.
        ms2::Log("coop: exception escaped net::Frame (suppressed)");
    }
}

}  // namespace

// Called once by the DLL's startup, on the worker thread, after ue::Init().
void Start() {
    if (g_started) return;
    g_started = true;

    if (!ue::IsReady()) {
        ms2::Log("coop::Start: ue::Init() did not succeed -- the co-op module stays off");
        return;
    }
    ms2::Logf("coop::Start: engine layer ready (%s)", ue::InitReport());

    // ---- 1. base layer: coop dir, identity, kill switches, stale files -------
    // This logs the frozen banner and writes result.txt = RUNNING.  It is also
    // where Toggles::Load() reads ANIM_OFF.txt .. GAME_THREAD.txt, once, exactly
    // like main.lua's load-time io.open calls.
    try {
        net::Start();
    } catch (...) {
        ms2::Log("coop::Start: net::Start threw -- the co-op module stays off");
        return;
    }

    // ---- 2. the per-frame tick ---------------------------------------------
    // Registered once.  All periodic work (net publish/apply, puppet drive, the
    // 2 Hz boot/status, the 1 Hz heartbeat) is inside coop::ticks(); nothing else
    // in this module ever asks the engine for a timer.
    if (!g_tickRegistered) {
        ue::OnTick(&OnEngineTick);
        g_tickRegistered = true;
    }

    // ---- 3. report ----------------------------------------------------------
    net::Log("coop: native module started (me=%s role=%s dir=%s)", state().local.me.c_str(),
             state().local.role.c_str(), state().local.dir.c_str());
}

}  // namespace coop
