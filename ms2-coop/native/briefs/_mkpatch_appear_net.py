"""Build briefs/patch-appear-coop_net.diff from the current src/coop_net.cpp.

appear's brief says: do not edit a file you do not own; write a unified diff
here instead.  This script applies the two fixes to an in-memory copy and emits
the diff, so the patch is guaranteed to apply to the file as it is on disk.
Run:  python _mkpatch_appear_net.py
"""
import difflib
import io
import os

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "src", "coop_net.cpp")
OUT = os.path.join(HERE, "patch-appear-coop_net.diff")

OLD_NOW = """double Now() {
    double t = 0.0;
    if (GameRealSeconds(&t)) return t;
    if (!g_clockWarned) {
        g_clockWarned = true;
        Log("real-time clock unavailable (GetRealTimeSeconds); using a monotonic clock");
    }
    return SteadySeconds();
}
"""

NOTE = """
*** WHY THIS FILE, AND WHAT IT IS NOT ***
This is `appear`'s patch.  It carries ONE fix: the `lookadd` diff must be retried,
because the AddItem spawn is deferred by a tick and the single same-tick diff left
appear::LOOK_ADDED empty -- which is why `lookdel` matched nothing and LOOK's off
half could never pass (evidence below).

It deliberately does NOT touch net::Now().  The broken game clock is the root cause
of the ANIM and STREAM failures too, but patch-combat-coop_net.diff and
patch-world-coop_net.diff already replace Now() with a monotonic clock; a third
version of the same hunk would only conflict.  Take combat's (it accepts
GetRealTimeSeconds while it is sane and non-decreasing, and falls back to the
steady clock otherwise).  The three files this brief owns are immune either way:
  * coop_puppet.cpp keeps its own steady clock (TickSeconds) for patrol / travel /
    tp-hold / stream-settle / drive-speed and reads the peer's packet age through
    PeerAge(), which ignores an insane PEER().at.
  * coop_local.cpp's AnimInstrument only counts what the puppet actually did.

*** EVIDENCE (A = E:\\Games\\_MS2-coop-test, B = E:\\Games\\_MS2-coop-b, 2026-10-25) ***
B/probe.log 03:29:33  lookadd queued: /Game/Sparta/Items/Pickups/BP_AIM_FlowerCrown...
B/probe.log 03:29:33  lookadd AddItem(...) param=ActorClass size=8 ok=true
                      ^ the actor does not exist yet: no `lookadd spawned` line ever
B/probe.log 03:29:34  look survey ... BP_AIM_FlowerCrown_C_2147467547_2147467548 ...
B/probe.log 03:29:34  own look -> 038dd00f (1 parts, 1002 bytes)      <- part is on
B/probe.log 03:29:50  lookdel
B/probe.log 03:29:50  cmd handled: lookdel
                      ^ and then NOTHING: appear::LookDel logged no "lookdel %s
                        (RemoveMe ok=...)" because g_lookAdded was empty, so the
                        crown stayed, B's `look` hash stayed 038dd00f and A's
                        `lookapplied`/`peerlookparts` had nothing new to follow.
B/sync.txt   (end)    look=038dd00f lookparts=1      (should be back to 2465c27f / 0)

*** HOW TO CONFIRM IT WORKED (next run-two) ***
  * B's log: `lookadd AddItem(...) ok=true` then, <=1 s later,
    `lookadd spawned BP_AIM_FlowerCrown_C_...:..._C_<n>_<n> (polled 1s after AddItem)`.
  * B's log on `lookdel`: `lookdel <same name> (RemoveMe ok=true, from LOOK_ADDED)
    hidden=true`.
  * B's `own look ->` must print 2465c27f (0 parts, 586 bytes) about a second later.
  * A's `peer look applied 2465c27f (0 parts)` and sync.txt on both clients:
    look=2465c27f lookparts=0 / peerlookparts=0.
"""

NEW_NOW = """double Now() {
    // GetRealTimeSeconds is the Lua's rt(), but in this build its ProcessEvent
    // return is not a clock.  Measured on both clients in the 2026-10-25 run
    // (coop\\probe.log): one wall second produced 0.0 -> -0.6 -> -160.0
    // (`spawnscan: clock rewound`, coop_combat's own guard) and the drive-hide
    // diagnostic printed now=3.7e35 / now=2.9e38.  Every timer built on it fired
    // early or never:
    //   coop_puppet patrol 14 s   -> `patrol: done` after 1 s
    //   coop_puppet travel 60 s   -> `travel arrived` in the same second
    //   coop_puppet tp hold 6 s   -> `tp hold released` after 1 s
    //   coop_puppet stream settle  -> verdict written 1 s into the 25 s window (FAIL)
    //   PEER().at garbage         -> puppet hidden/visible ~3x/s (1583 pairs on A)
    // A monotonic clock is what those timers need, and unlike the game clock it
    // cannot rewind on a level load (README: "clock rewinds on level load";
    // "GetRealTimeSeconds vs os.time").  The game clock is still probed, once,
    // so the log says what it reads.
    const double steady = SteadySeconds();
    if (!g_clockWarned) {
        g_clockWarned = true;
        double t = 0.0;
        if (GameRealSeconds(&t)) {
            Log("game clock (GetRealTimeSeconds) reads %.2f at monotonic %.2f -- timers use"
                " the monotonic clock", t, steady);
        } else {
            Log("real-time clock unavailable (GetRealTimeSeconds) -- timers use the monotonic"
                " clock");
        }
    }
    return steady;
}
"""

OLD_JOB = """struct LookAddJob {
    bool active = false;
    std::string path;
    int64_t at = 0;
};
"""

NEW_JOB = """struct LookAddJob {
    bool active = false;
    std::string path;
    int64_t at = 0;
    // The AddItem spawn is DEFERRED: the attachable is not in the object array in
    // the tick that asked for it (probe.log 2026-10-25 03:29:33 `lookadd AddItem
    // ok=true`, the crown's first `look survey` at 03:29:34).  The diff below was
    // taken once, in that tick, found nothing and reset LOOK_ADD -- so
    // appear::SetLookAdded never ran, `lookdel` matched no actor, the crown was
    // never removed, B's look hash never changed and LOOK's off half could not
    // pass.  Polling is the faithful port of the Lua's LOOK_ADDED.
    bool polling = false;
    std::string cls;                    // the class the diff is taken on
    int64_t until = 0;
    std::vector<std::string> before;    // ClassInstances(cls) at AddItem time
};
"""

OLD_LOOKTICK = """void LookTestTick() {
    if (!g_lookAdd.active) return;
    if (Wall() - g_lookAdd.at > 40) {
        Log("lookadd gave up: %s", g_lookAdd.path.c_str());
        g_lookAdd = LookAddJob();
        return;
    }
    ue::UObject* cls = Find(g_lookAdd.path, true);
    if (!cls) return;

    ue::UObject* pawn = Pawn();
    ue::UObject* bpc = pawn ? uecall::PropObj(pawn, "BPC_AttachableItems") : nullptr;
    if (!Valid(bpc)) return;

    // The Lua snapshots LOOK.attachablesOf(pawn); the class we were asked to add
    // is the one that appears, so its own instances are the exact diff and no
    // class list has to be duplicated here.
    const std::string clsName = ue::GetName(cls);
    const std::vector<std::string> before = ClassInstances(clsName);
"""

NEW_LOOKTICK = """void LookTestTick() {
    if (!g_lookAdd.active) return;
    const int64_t nowWall = Wall();
    if (nowWall - g_lookAdd.at > 40) {
        Log("lookadd gave up: %s", g_lookAdd.path.c_str());
        g_lookAdd = LookAddJob();
        return;
    }
    ue::UObject* pawn = Pawn();
    if (!Valid(pawn)) return;

    // Phase 2: AddItem already ran; wait for the deferred spawn to show up and
    // remember it as LOOK_ADDED (main.lua:2074-2076).
    if (g_lookAdd.polling) {
        for (const std::string& name : ClassInstances(g_lookAdd.cls)) {
            if (!HasName(g_lookAdd.before, name)) {
                appear::SetLookAdded(name);
                Log("lookadd spawned %s (polled %ds after AddItem)", name.c_str(),
                    (int)(nowWall - g_lookAdd.at));
                g_lookAdd = LookAddJob();
                return;
            }
        }
        if (nowWall > g_lookAdd.until) {
            Log("lookadd: AddItem made no new attachable within 5s -- lookdel has nothing to"
                " remove (%s)", g_lookAdd.path.c_str());
            g_lookAdd = LookAddJob();
        }
        return;
    }

    ue::UObject* cls = Find(g_lookAdd.path, true);
    if (!cls) return;

    ue::UObject* bpc = uecall::PropObj(pawn, "BPC_AttachableItems");
    if (!Valid(bpc)) return;

    // The Lua snapshots LOOK.attachablesOf(pawn); the class we were asked to add
    // is the one that appears, so its own instances are the exact diff and no
    // class list has to be duplicated here.
    const std::string clsName = ue::GetName(cls);
    const std::vector<std::string> before = ClassInstances(clsName);
    // Arm the retry: the spawn is deferred, so the same-tick diff below is
    // usually empty and this is what fills LOOK_ADDED in.
    g_lookAdd.cls = clsName;
    g_lookAdd.before = before;
    g_lookAdd.polling = true;
    g_lookAdd.until = Wall() + 5;
"""

OLD_TAIL = """    const std::vector<std::string> after = ClassInstances(clsName);
    for (const std::string& name : after) {
        if (!HasName(before, name)) {
            appear::SetLookAdded(name);
            Log("lookadd spawned %s", name.c_str());
        }
    }
    g_lookAdd = LookAddJob();
}
"""

NEW_TAIL = """    // Same-tick diff, kept: when the game does spawn synchronously this is the
    // answer and the poll above never runs.  Otherwise the job stays armed for
    // up to 5 s and phase 2 fills LOOK_ADDED in.
    const std::vector<std::string> after = ClassInstances(clsName);
    for (const std::string& name : after) {
        if (!HasName(before, name)) {
            appear::SetLookAdded(name);
            Log("lookadd spawned %s", name.c_str());
            g_lookAdd = LookAddJob();
            return;
        }
    }
}
"""

OLD_CMD = """        } else if (cmd == "lookadd") {
            g_lookAdd.active = true;
            g_lookAdd.path = rest;
            g_lookAdd.at = Wall();
            Log("lookadd queued: %s", rest.c_str());
"""

NEW_CMD = """        } else if (cmd == "lookadd") {
            g_lookAdd = LookAddJob();          // never inherit a previous poll's state
            g_lookAdd.active = true;
            g_lookAdd.path = rest;
            g_lookAdd.at = Wall();
            Log("lookadd queued: %s", rest.c_str());
"""


def main():
    with io.open(SRC, "r", encoding="utf-8", newline="") as f:
        text = f.read()
    crlf = "\r\n" in text
    work = text.replace("\r\n", "\n")

    for name, old, new in (
        ("LookAddJob", OLD_JOB, NEW_JOB),
        ("net::LookTestTick head", OLD_LOOKTICK, NEW_LOOKTICK),
        ("net::LookTestTick tail", OLD_TAIL, NEW_TAIL),
        ("cmd lookadd", OLD_CMD, NEW_CMD),
    ):
        if work.count(old) != 1:
            raise SystemExit("hunk %r matched %d times" % (name, work.count(old)))
        work = work.replace(old, new)

    a = text.replace("\r\n", "\n").splitlines(keepends=True)
    b = work.splitlines(keepends=True)
    diff = difflib.unified_diff(a, b, fromfile="a/src/coop_net.cpp", tofile="b/src/coop_net.cpp", n=3)
    out = "".join(diff) + NOTE
    # difflib's context lines are fine as-is; keep LF (patch(1)/git apply accept it
    # on a CRLF file because every changed line is a full line).
    with io.open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(out)
    print("wrote %s (%d lines, crlf source=%s)" % (OUT, out.count("\n"), crlf))


if __name__ == "__main__":
    main()
