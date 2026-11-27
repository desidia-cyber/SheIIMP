// coop_state.h -- shared contract for the native Mortal Shell II co-op DLL.
//
// This header is the ONLY thing the porting modules share. It is deliberately
// dependency-free: it includes <cstdint> <string> <vector> <functional> and
// nothing else, and it forward-declares the ue:: types it mentions. Every
// translation unit of the port can include it; no module includes another
// module's header.
//
// ---------------------------------------------------------------------------
// Rules of the contract (read docs/NATIVE-PORT.md section 3 before coding)
// ---------------------------------------------------------------------------
// 1. `coop::state()` is the single global state. Every sub-struct is public so
//    a module can READ another module's state, but only the owning module's
//    Tick*() may WRITE it. The owner is named on each struct.
//
// 2. Only coop_net touches the filesystem and the wire. A module contributes to
//    the state line by implementing its named writer (see `PacketWriters`),
//    which appends e.g. `,"ws":[...]` to the buffer. Writers are invoked by
//    net::Publish() in the frozen order below -- do not reorder, the relay and
//    the receiver depend on it.
//
//        {"t","id","sq","x","y","z","vx","vy","vz","yaw","pitch","anim",
//         "spd","mseq","mpos","mrate","mid","body","apart","wpn","wsock",
//         "wpns","lk"}                       <- built by coop_net / coop_local
//        + combat::SerializeNpc   ",npc":[...]
//        + combat::SerializePd    ",pd":%.3f
//        + combat::SerializeNhp   ",nhp":[...]
//        + combat::SerializeDead  ",dead":[...]
//        + world::SerializeWs     ",ws":[...]
//        + progress::SerializeWb  ",wb":[...] then ",wq":[...] then ",wd":[...]
//        + world::SerializeLift   ",wl":[...]
//        + world::SerializeChest  ",wc":[...]
//      (the whole world block is omitted when Toggles::wsEnabled is false)
//      + [R5-BODY] zone::SerializeZn ",zn":"<map>|<zone>|<liHash>" and
//        body::SerializeBh ",bh":[14 numbers] -- last, after the world block, so
//        an older receiver that stops reading at "wc" is unaffected.
//
// 3. Only coop_net reads the peer line. It calls, in this order:
//        combat::OnPeerLine()  -> progress::OnPeerLine() -> world::OnPeerLine()
//    which is the order of the Lua (applyNPCs, applyHealth, applyDead, then
//    ws, wp, lift, chest). A module parses only its own sections, with
//    net::Section()/net::KvNum()/net::KvStr().
//
// 4. Timing: `net::Now()` is the game's real-time clock (GetRealTimeSeconds) and
//    is the ONLY clock for play heads, extrapolation, settle windows and rate
//    limits. `net::Wall()` is wall seconds (os.time) and is used only where Lua
//    used os.time(). Never use CPU time (Lua's os.clock() was CPU time and ran
//    several times faster -- docs/NATIVE-PORT.md section 4, item 7).
//
// 5. Every cached ue::UObject* is invalidated on a world change. A module must
//    implement Reset() and coop_net calls all of them: this is the crash
//    defence that cost a guest client a session in Lua (docs/HANDOFF.md 18).
//
// 6. Log lines, file names, sync.txt keys and number formatting are frozen.
//    See docs/NATIVE-PORT.md section 2.
// ---------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// [R5-DEATH] begin
// The pure death-rule decision functions (party case, suppression, respawn,
// re-case, the stack arithmetic).  Included here so a module can read
// state().death without re-declaring the vocabulary, and so the SAME header the
// unit test checks is the one the DLL ships.
#include "coop_policy.h"
// [R5-DEATH] end

// The engine-access layer. Only forward declarations are needed here; the
// definitions live in ue:: (the agent writing that layer owns them).
namespace ue {
// `struct`, not `class`: ms2_ue.h declares `struct UObject`, and MSVC mangles a
// class-key mismatch into a DIFFERENT type symbol, which breaks every link that
// crosses the two headers (C4099 is the warning; the link failure is the bug).
struct UObject;
struct FVector;    // double X, Y, Z in UE5
struct FRotator;   // double Pitch, Yaw, Roll
struct FQuat;      // double X, Y, Z, W
struct FTransform; // Rotation, Translation, Scale3D
}

namespace coop {

// ===========================================================================
// 1. math
// ===========================================================================

struct Vec3 {
    double X = 0.0, Y = 0.0, Z = 0.0;
};

struct Rot {
    double Pitch = 0.0, Yaw = 0.0, Roll = 0.0;
};

struct Quat {
    double X = 0.0, Y = 0.0, Z = 0.0, W = 1.0;
};

struct Scale3 {
    double X = 1.0, Y = 1.0, Z = 1.0;
};

struct Xform {
    Vec3 T;
    Quat R;
    Scale3 S;
};

// Implemented by the ue adapter (one .cpp, next to the ue:: layer). Declarations
// only, so this header stays free of ue:: includes.
Vec3 ToVec3(const ue::FVector& v);
ue::FVector FromVec3(const Vec3& v);
Rot ToRot(const ue::FRotator& r);
ue::FRotator FromRot(const Rot& r);
Quat ToQuat(const ue::FQuat& q);
ue::FQuat FromQuat(const Quat& q);
Xform ToXform(const ue::FTransform& t);
ue::FTransform FromXform(const Xform& t);

// Ported verbatim from main.lua (see the line refs).
double Dist(const Vec3& a, const Vec3& b);              // main.lua:170
Quat RotQuat(const Rot& r);                             // main.lua:870  (UE FRotator::Quaternion)
Quat QuatMul(const Quat& a, const Quat& b);             // main.lua:878  (a * b : b applied first)
Vec3 QuatRot(const Quat& q, const Vec3& v);             // main.lua:884
Xform Compose(const Xform& parent, const Xform& child); // main.lua:900  parent * child
double NormDeg(double a);                               // main.lua:391  (-180, 180]

// ===========================================================================
// 2. enums
// ===========================================================================

enum class Phase {
    Menu = 0,   // waiting for WBP_MainMenu_C, then ContinueGame
    World,      // waiting for the player pawn
    Summon,     // snapshot BP_PlayerCharacter_C, run the summon console command
    Adopt,      // diff names, adopt the new actor as the puppet
    Sync        // normal operation
};

enum class Guard {
    None = 0,   // never skipped
    Settle,     // skipped while net::WorldSettle() is true  (Lua SETTLE_SKIP)
    Hold        // skipped while net::LocalHold() is true    (Lua HOLD_SKIP)
};

// How a replicated value arrived; drives the receiver's apply path.
enum class How {
    Live = 0,   // flipped during this session -> replay the game's own action
    Base,       // was already true in the peer's save -> set the flag only
    Ledger      // came from the local store-A ledger
};

// ===========================================================================
// 3. constants -- every one of these is load-bearing (docs/NATIVE-PORT.md 4)
// ===========================================================================

// puppet drive (main.lua:70-74)
//
// NOTE: TELEPORT_DIST / STOP_DIST / MAX_SPEED / MIN_SPEED / STUCK_TICKS are
// DECLARED AND NEVER READ by main.lua -- they are the leftovers of the abandoned
// AddMovementInput drive, which crashes the game for a controller-less pawn
// (docs/HANDOFF.md 5.6). Do not implement them, do not reintroduce input
// driving; the puppet is positioned directly. They are kept here only so a
// grep for the Lua names finds the note.
constexpr double kLegacyTeleportDist = 900.0; // unused
constexpr double kLegacyStopDist     = 70.0;  // unused
constexpr double kLegacyMaxSpeed     = 900.0; // unused
constexpr double kLegacyMinSpeed     = 140.0; // unused
constexpr int    kLegacyStuckTicks   = 45;    // unused
// The constants the drive actually uses:
constexpr double kSnapDist     = 400.0;   // driveTick: snap beyond this (main.lua:1700)
constexpr double kEasePerTick  = 0.35;    // driveTick: otherwise ease (main.lua:1705)
// [R8-WALL] the drive's ease while the peer squeezes through a gap: stiffer than
// kEasePerTick so the body follows the guided move closely, and the snap above is
// skipped for its duration (the capsule is warped in the gap, not carried by
// velocity, so a snap would teleport it through the wall).
constexpr double kTraversalEase = 0.60;
constexpr double kMaxExtrapAge = 0.25;    // driveTick: extrapolation clamp (main.lua:1695)
constexpr double kYawEase      = 0.45;    // driveTick: yaw easing (main.lua:1690)
constexpr double kYawSnapDeg   = 120.0;   // driveTick: yaw snap threshold (main.lua:1690)
constexpr double kPeerStale    = 3.0;     // driveTick: hide the body after this (main.lua:1678)

// animation (main.lua:204-212)
constexpr double kRunSpeed     = 520.0;   // >= this : sprint clip
constexpr double kWalkSpeed    = 45.0;    // >= this : walk clip
constexpr double kWalkRateDiv  = 180.0;   // walk play rate = speed / 180
constexpr double kSprintRateDiv= 800.0;   // sprint play rate = speed / 800
constexpr double kWalkRateMin  = 0.6;     // main.lua:486  clamp (a rate of 0 freezes the clip)
constexpr double kWalkRateMax  = 2.2;
constexpr double kSprintRateMin= 0.7;     // main.lua:488
constexpr double kSprintRateMax= 1.6;
constexpr double kMontageRateMin = 0.1;   // main.lua:448
constexpr double kMontageLead  = 0.5;     // playMontage: age clamp (main.lua:434)
constexpr double kMontageSlack = 0.12;    // playMontage: re-seek threshold (main.lua:444)
constexpr int    kBoneScanMax  = 400;     // footBonesOnMesh cap (main.lua:295)

// enemies (main.lua:1253-1255)
constexpr int    kMaxNpc       = 16;      // MAX_NPC
constexpr double kNpcRadius    = 4000.0;  // NPC_RADIUS
constexpr double kNpcMargin    = 1300.0;  // NPC_MARGIN
constexpr int    kDeadListCap  = 40;      // deadJSON cap (main.lua:1370)
constexpr double kNpcFarMargin = 300.0;   // host yield / guest claim offsets
constexpr double kMirrorTimeout= 2.0;     // hand an enemy back after this (main.lua:3953)
constexpr double kAiTargetRange= 3000.0;  // aiTargetPeer: dp < 3000 (main.lua:3882)
constexpr double kHpAdoptEps   = 0.02;    // applyHealth adoption epsilon
constexpr double kHpLocalEps   = 0.004;   // sampleCombat damage epsilon
constexpr double kHpRespawn    = 0.9;     // "alive again" threshold
constexpr double kHpFreshGuard = 0.995;   // puppetDamage ignores >= this
constexpr double kPdEps        = 0.0005;  // applyPeerDamage epsilon

// readi+
constexpr double kReadyHold    = 5.0;     // PLAY_READY needs 5 s (main.lua:1558)
constexpr double kLocalHoldSecs= 3.0;     // LOCAL_HOLD after a hard signal (main.lua:1551)
constexpr double kWorldSettle  = 1.5;     // after a data-layer switch (main.lua:3099)
constexpr double kTravelHoldSecs = 6.0;   // TP_HOLD (main.lua:3757)

// appearance (main.lua:992, 919)
constexpr int    kLookMaxParts = 24;
constexpr int    kLookMaxMats  = 16;
constexpr double kWeaponRetry  = 5.0;     // s (main.lua:4106)
constexpr double kLookRetry    = 3.0;     // s (main.lua:4116)
constexpr double kLookDebounce = 2.0;     // 2 Hz capture, publish after 2 equal samples
constexpr double kGameLoadGap  = 2.0;     // asset loader min interval (main.lua:267)
constexpr int    kGameLoadTries= 3;

// world state -- store A (main.lua:2245-2250)
constexpr double kLiveSecs     = 20.0;    // WS_LIVE_SECS
constexpr int    kWsWindow     = 40;      // WS_WINDOW
constexpr double kWsSettle     = 8.0;     // WS_SETTLE: ignore values before this age
constexpr int    kWsApplyBudget= 6;       // applies per 2 Hz tick
// [R5-WORLD] begin
// 35 = the Lua's 28 + 7 classes G1 adds to the boolean table (BPSO_NPC_C,
// BPSO_ShellRevive_C, BPSO_PlayerTrapDungeon_C, BPSO_PetrifiedCultistManager_C,
// BPSO_MovingObject_C, BPSO_TavernArena_C, BPSO_SavedLockedChests_C) + 1 more for
// the boolean half of BPSO_WoodenElevator_C (HasBrokenOnce; its int32
// StartingFloor is in the typed table).  The typed ints/string-arrays live in a
// separate table in coop_world_ws2.cpp.
// [R5-WORLD] end
constexpr int    kWsKindCount  = 36;      // WS_KINDS entries -> 18 ticks per full pass

// world progression -- store B / quests / layers (main.lua:2647)
constexpr double kWpSettle     = 10.0;    // WP_SETTLE: prime time
constexpr int    kWbWindow     = 12;
constexpr int    kWqWindow     = 16;
constexpr int    kWdWindow     = 16;
constexpr int    kWbApplyBudget= 6;
constexpr int    kWqApplyBudget= 4;
constexpr int    kWdApplyBudget= 2;
constexpr double kWdApplyCd    = 10.0;    // per-layer cooldown
constexpr double kWdFarGate    = 15000.0; // skip layer sync when players are farther
constexpr double kWqStageWait  = 8.0;     // WQ_STAGE_WAIT

// lifts / chests (main.lua:3399, 3515)
constexpr double kLiftPrime     = 10.0;
constexpr double kLiftReplayWin = 6.0;    // a replay edge is ignored for 6 s
constexpr double kLiftMovedZ    = 1000.0; // verdict: cabin moved this much
constexpr double kChestSettle   = 8.0;
constexpr double kChestLootSecs = 8.0;
constexpr double kChestKeepSecs = 40.0;
constexpr int    kChestSnaps    = 6;
constexpr int    kChestQueueTries = 60;

// stream verdict (main.lua:3781)
constexpr double kStreamFarDist = 30000.0;
constexpr double kStreamSettle  = 25.0;
constexpr double kStreamReassert= 10.0;
constexpr double kStreamActorsRadius = 15000.0;

// ledger (main.lua:2371-2381)
constexpr int    kLedgerBadDrop = 10;
constexpr double kLedgerTrustSecs = 45.0;
constexpr double kLedgerSaveGap  = 5.0;

// boot timeouts (main.lua:570, 580, 604)
constexpr double kMenuTimeout     = 300.0;
constexpr double kGameplayTimeout = 300.0;
constexpr double kSummonTimeout   = 20.0;
constexpr double kGameplayMinWait = 4.0;

// ===========================================================================
// 4. small containers (no <map>: lookups are linear over <= a few hundred rows)
// ===========================================================================

struct StrInt {
    std::string key;
    int value = 0;
};

struct StrNum {
    std::string key;
    double value = 0.0;
};

struct StrStr {
    std::string key;
    std::string value;
};

class StrIntMap {
public:
    int* Find(const std::string& key);
    const int* Find(const std::string& key) const;
    int Get(const std::string& key, int def = 0) const;
    void Set(const std::string& key, int v);
    bool Erase(const std::string& key);
    std::size_t Size() const { return items_.size(); }
    void Clear() { items_.clear(); }
    const std::vector<StrInt>& Items() const { return items_; }

private:
    std::vector<StrInt> items_;
};

class StrNumMap {
public:
    double* Find(const std::string& key);
    const double* Find(const std::string& key) const;
    double Get(const std::string& key, double def = 0.0) const;
    void Set(const std::string& key, double v);
    bool Erase(const std::string& key);
    std::size_t Size() const { return items_.size(); }
    void Clear() { items_.clear(); }
    const std::vector<StrNum>& Items() const { return items_; }

private:
    std::vector<StrNum> items_;
};

class StrSet {
public:
    bool Has(const std::string& key) const;
    bool Add(const std::string& key); // true when newly added
    bool Erase(const std::string& key);
    std::size_t Size() const { return items_.size(); }
    void Clear() { items_.clear(); }
    const std::vector<std::string>& Items() const { return items_; }

private:
    std::vector<std::string> items_;
};

// string -> string, insertion ordered.  Added for CombatState::spawnAlias, whose
// Lua value is a PAIR (SPN_ALIAS[enemy tail] = "SP_<spawner tail>", main.lua:1279,
// 3836-3839) that a StrSet cannot carry.
class StrStrMap {
public:
    std::string* Find(const std::string& key);
    const std::string* Find(const std::string& key) const;
    std::string Get(const std::string& key, const char* def = "") const;
    void Set(const std::string& key, const std::string& v);
    bool Erase(const std::string& key);
    std::size_t Size() const { return items_.size(); }
    void Clear() { items_.clear(); }
    const std::vector<StrStr>& Items() const { return items_; }

private:
    std::vector<StrStr> items_;
};

// ===========================================================================
// 5. toggles -- the kill switches, read ONCE at load from the coop dir
//    (main.lua:255, 383, 511, 663, 669, 855, 2304, 2624, 3359, 3514, 3814, 4041)
// ===========================================================================

struct Toggles {
    bool animOff        = false; // ANIM_OFF.txt
    bool weaponOff      = false; // WEAPON_OFF.txt
    bool wsEnabled      = true;  // !WS_OFF.txt            (if false: no ws/wb/wq/wd/wl/wc)
    bool streamEnabled  = true;  // !STREAM_OFF.txt
    bool lookEnabled    = true;  // !LOOK_OFF.txt
    bool loadOn         = false; // LOAD_ON.txt             (default OFF)
    bool wsLedgerOn     = true;  // !WS_LEDGER_OFF.txt
    // [R5-WORLD] begin -- G1/G1b: store-A2 (new save-object classes + the typed
    // "wt" section).  Switching it off restores the round-4 behaviour exactly:
    // the 7 extra boolean kinds stop being scanned *and* the 5 extra fields on
    // already-watched classes are ignored, and no "wt" section is built or read.
    bool ws2Enabled     = true;  // !WS2_OFF.txt
    // [R5-WORLD] end
    bool qsEnabled      = true;  // !QS_OFF.txt
    bool dlEnabled      = true;  // !DL_OFF.txt
    bool liftEnabled    = true;  // !LIFT_OFF.txt
    bool chestEnabled   = true;  // !CHEST_OFF.txt
    bool mirrorEnabled  = true;  // !MIRROR_OFF.txt
    bool gameThread     = false; // GAME_THREAD.txt         (default OFF; crashed clients)
    // [R5-BODY] begin -- the two kill switches of the R5 body lane
    bool zoneGuard      = true;  // !ZONE_OFF.txt  (guard off: still publish "zn")
    bool bodySync       = true;  // !BODY_OFF.txt  (no "bh" publish, no puppet write)
    // [R5-BODY] end
    // [R7-BREAK] begin -- the round-7 live-breakables lane.  With BREAK_OFF.txt
    // present nothing is scanned, no "wx" section is written or read and no apply
    // runs, so the build behaves exactly like round 6 (coop_world_break.cpp).
    bool breakEnabled   = true;  // !BREAK_OFF.txt
    // [R7-BREAK] end
    // [R7-WIRE] the packet split (one publish -> N datagrams of <= ~1.2 kB).
    // Off restores the pre-round-7 single-datagram line exactly, which is also
    // what an old relay needs; the receiver reads both shapes.
    bool wireSplit      = true;  // !WIRE_SPLIT_OFF.txt
    // [R7-WIRE] end
    // [R7-BOSS] begin -- the boss-phase lane's kill switch: no "bsp" key on the wire,
    // no phase call on the peer, no phase hooks installed.
    bool bossSync       = true;  // !BOSS_OFF.txt
    // [R7-BOSS] end
    // [R7-DIALOG] begin -- dialogue and cinematics are two independent lanes
    bool dlgEnabled     = true;  // !DIALOG_OFF.txt (no "dl"/"ct", no puppet effect)
    bool cineEnabled    = true;  // !CINE_OFF.txt   (no "ci", no puppet effect)
    // [R7-DIALOG] end
    // [R7-DROPS] begin -- G6.  Off restores the pre-round-7 behaviour exactly: no
    // "drp" section, no grant on the receiver, and the mirrored-death veto is not
    // even installed (so the non-owner rolls its own loot again, as it used to).
    bool dropsEnabled   = true;  // !DROPS_OFF.txt
    // [R7-DROPS] end
    // [R8-APPEAR] the peer-asset loader (coop_load.cpp): with APPEAR_OFF.txt
    // present a look part / shell mesh / weapon whose asset this client never
    // loaded stays missing (round-7 behaviour) instead of being loaded on demand.
    bool assetLoad      = true;  // !APPEAR_OFF.txt
    // [R8-APPEAR] end
    // [R8-WALL] begin -- the traversal ("go through the broken wall") lane: no "wa"
    // on the wire, no traversal hold on the puppet, no traversal follow.  Off makes
    // the build behave exactly like round 7 (coop_wall_anim.cpp).
    bool wallAnim       = true;  // !WALLANIM_OFF.txt
    // [R8-WALL] end
    // [R8-MONEY] the death-drop lane (the lost-Gloom spoils): off = no sampling, no
    // scan, no stale-drop sweep, no "money=" line -- the pre-round-8 build exactly.
    bool moneyEnabled   = true;  // !MONEY_OFF.txt
    // [R8-MONEY] end
    // [R8-FARENEMY] the far-replayed-enemy lane: with FARENEMY_OFF.txt present the
    // replayed enemy keeps the life span the game gave it, so the behaviour is
    // exactly the round-7 one (the copy is destroyed ~1 s after it spawns).  The
    // watch/log lines stay on either way -- they are the instrument the verdict reads.
    bool farEnemy       = true;  // !FARENEMY_OFF.txt
    // [R8-FARENEMY] end
    // [R10] Part A + Part B feature toggles (all default ON, each independently kill-switchable)
    bool hitHook        = true;  // !HITHOOK_OFF.txt
    bool puppetPlus     = true;  // !PUPPET_PLUS_OFF.txt
    bool menuEnabled    = true;  // !MENU_OFF.txt
    bool beaconScale    = true;  // !BEACON_SCALE_OFF.txt
    bool fxGrabs        = true;  // !FX_GRABS_OFF.txt
    bool partB          = true;  // !PARTB_OFF.txt
    // [R10] end

    void Load(); // reads <dir>/<name>.txt for every row above, once
};

// ===========================================================================
// 6. module state
// ===========================================================================

// ---------------------------------------------------------------------------
// owner: coop_local   (main.lua:24-153, 154-187, 609-785, 1453-1564, 1984-2054)
// ---------------------------------------------------------------------------
struct MontageSig {
    std::string path;      // segment animation asset path, "" = none
    double pos = 0.0;      // mpos
    double rate = 1.0;     // mrate
    int id = 0;            // mid (bumped when the montage changes or the head rewinds)
};

struct NamedActor {
    std::string tail;
    ue::UObject* obj = nullptr;
};

struct LocalState {
    // --- identity (written once by net::Start) ---
    std::string me;      // "A"/"B"/... from playerid.txt
    std::string peer;    // the other id (Lua: (ME=="A")?"B":"A")
    std::string role;    // "host" | "guest"
    std::string dir;     // coop dir, trailing '/'

    // --- readiness (main.lua:1507-1564) ---
    bool ready = false;
    int64_t readySince = 0;       // wall seconds; 0 = not counting
    std::string readyWhy;         // "ready" | "settling" | a hard/soft reason

    // --- hot fields (main.lua:1566-1606) ---
    int64_t seq = 0;              // SEND_SEQ (increments even in the not-ready shape)
    Vec3 pos;
    Rot rot;
    Vec3 vel;                     // possibly synthesised (main.lua:1583)
    double speed = 0.0;
    bool hasSentPos = false;
    Vec3 sentPos;
    std::string anim;             // "idle"|"walk"|"run"|"action"
    MontageSig montage;
    std::string body;             // MYBODY   own shell mesh path
    std::string apart;            // MYPART   own AnimBP path
    std::string wpn;              // MYWPN    first weapon mesh path
    std::string wsock;            // MYWSOCK  its socket
    std::string wpns;             // MYWPNS   the whole weapon array section body

    // --- actor caches (15 Hz; main.lua:628-651) ---
    std::vector<ue::UObject*> tickChars;   // FindAllOf("Character")
    std::vector<NamedActor> tickTails;     // actorTail -> actor (puppet excluded)

    // --- bookkeeping ---
    double lastPeerWarnAt = 0.0;   // peerWatchdog
    int goodTicks = 0, totalTicks = 0;   // judge() (drive counters live in PuppetState)

    // --- animation telemetry (main.lua:1984-2017) ---
    int animIdle = 0, animWalk = 0, animSprint = 0, animMontage = 0;
    int animMoveTicks = 0, animStillTicks = 0;
    bool animVerdictWritten = false;
    bool verdictWritten = false;
};

// ---------------------------------------------------------------------------
// owner: coop_net   (main.lua:1409-1451, 1936-1983)
// The parsed peer packet. Read by everybody, written only by net::ApplyPeer.
// ---------------------------------------------------------------------------
struct PeerState {
    bool present = false;   // a line was parsed at least once
    bool hasX = false;      // the line carried "x"
    bool live = false;      // its "sq" has advanced since this session started
    bool ready = true;      // false when the peer sent {"rdy":0}
    int64_t sq = -1;
    Vec3 pos;
    Vec3 vel;
    Rot rot;
    double speed = 0.0;
    double at = 0.0;        // net::Now() of the last fresh packet (stale when not fresh)
    int64_t wallAt = 0;     // net::Wall() of the last parse
    std::string body;       // last non-empty "body"
    std::string wpn;        // last non-empty "wpn"
    std::string wsock;      // last non-empty "wsock"
    std::string weapons;    // last "wpns" section payload (raw, incl. brackets)
    std::string look;       // last "lk" string
    bool hasMontage = false;
    MontageSig montage;     // PEER_MONTAGE
    // Player data (damage the peer's world dealt to our body), cumulative.
    bool hasPd = false;
    double pd = 0.0;
};

// ---------------------------------------------------------------------------
// owner: coop_puppet  (main.lua:188-606, 1607-1765, 3738-3811)
// ---------------------------------------------------------------------------
struct PuppetState {
    ue::UObject* actor = nullptr;      // the summoned BP_PlayerCharacter_C we drive
    ue::UObject* mesh = nullptr;       // ANIM_MESH: its skeletal mesh component
    ue::UObject* puppetMesh = nullptr; // PUPPET_MESH: cached component
    std::string appliedBody;           // APPLIED_BODY (shell path currently on the mesh)
    bool bodyReady = false;            // BODY_STATE == "ready"
    int bodyAttempts = 0;
    bool hidden = true;                // PUPPET_HIDDEN -- the single source of truth
    Phase phase = Phase::Menu;
    int64_t phaseEntered = 0;          // wall seconds
    int phaseNoted = 0;                // seconds already logged

    // animation state (main.lua:215)
    std::string animState;             // "" | "idle" | "walk" | ... | "m:<id>:<path>"
    double animRate = -1.0;
    std::string animKind;              // "idle"|"walk"|"sprint"|"montage"
    bool animModeSet = false;
    std::vector<std::string> footBones;

    // drive
    double puppetSpeed = 0.0;          // smoothed, for telemetry
    int driveInputs = 0;               // main.lua:1611 (read by local::judge)
    int driveTeleports = 0;            // main.lua:1611 (read by local::judge)
    bool hasPrevPos = false;
    Vec3 prevPos;
    double lastDriveAt = 0.0;
    bool puppetYawSet = false;
    double puppetYaw = 0.0;
    bool peerEverSeen = false;
    int stuckTicks = 0;

    // streaming sources (main.lua:514)
    std::string streamSrc = "none";
    double streamChecked = 0.0;
    bool streamFarSet = false;
    double streamFarAt = 0.0;
    int streamN0 = -1, streamN1 = -1;
    std::string streamVerdict;
    bool streamTravelled = false;
    bool hasLastPl = false;
    Vec3 lastPl;

    // travel / hold (main.lua:1649-1666, 3756-3762)
    bool travelActive = false;
    ue::UObject* travelPawn = nullptr;
    Vec3 travelFrom, travelTo;
    double travelT0 = 0.0, travelDur = 0.0;
    bool hasTpHold = false;
    ue::UObject* tpHoldPawn = nullptr;
    double tpHoldAt = 0.0;

    // patrol (main.lua:1731-1765)
    bool patrolActive = false;
    Vec3 patrolOrigin;
    double patrolT0 = 0.0, patrolDur = 0.0, patrolRadius = 260.0, patrolSpeed = 260.0;

    // asset loader (main.lua:253-277)
    bool loadBusy = false;
    int64_t loadBusyAt = 0, loadNextAt = 0;
    int loadDone = 0;
    StrIntMap loadTries;
    StrSet missingAssets;

    // sequence cache (main.lua:213): key -> object
    std::vector<NamedActor> sequences;
};

// ---------------------------------------------------------------------------
// owner: coop_appear  (main.lua:652-1191)
// ---------------------------------------------------------------------------
struct WeaponEntry {
    std::string path;   // asset path
    char kind = 'K';    // 'K' skeletal / 'S' static
    std::string socket;
    Vec3 loc;
    Rot rot;
};

struct LookPart {
    char kind = 'K';
    std::string asset;
    std::string socket;   // "@root" means the actor root with socket "None"
    Vec3 loc;
    Quat quat;
    Scale3 scale;
    bool leader = false;
    std::string mats;     // comma separated material paths
    std::string key;      // the raw "p=..." payload; the diff key AND the wire text
};

struct LookSig {
    std::string bodyMats;         // "bm=" payload
    std::vector<LookPart> parts;
    std::string raw;              // the exact string sent on the wire
};

struct AppearState {
    // own side (look sender)
    std::string mine;             // LOOK.mine (last published)
    std::string cand;             // LOOK.cand
    int candN = 0;                // LOOK.candN
    int nparts = 0;               // LOOK.nparts
    // peer side (look receiver). LOOK.comps / LOOK.pendMats are two parallel
    // vectors keyed by the raw "p=..." payload, because this header cannot use
    // <map>. Lookups are linear over <= kLookMaxParts entries.
    std::vector<std::string> compKeys;
    std::vector<ue::UObject*> comps;
    std::vector<std::string> pendKeys;
    std::vector<std::string> pendMats;
    ue::UObject* owner = nullptr; // LOOK.owner (the puppet the parts belong to)
    std::string applied;          // LOOK.applied
    std::string bodyKey;          // LOOK.bodyKey ("<APPLIED_BODY>#<bm>")
    int peerParts = 0, missing = 0, hiddenOwn = 0;
    double retryAt = 0.0;
    ue::UObject* silenced = nullptr;

    // weapons (peer side)
    std::vector<ue::UObject*> weaponComps;
    std::vector<std::string> weaponSockets;   // parallel to weaponComps (re-attach check)
    std::string weaponsApplied;   // WEAPON.applied
    double weaponRetryAt = 0.0;

    // log dedupe
    StrSet logged;
};

// ---------------------------------------------------------------------------
// owner: coop_combat  (main.lua:1192-1408, 1766-1935, 3808-3956)
// ---------------------------------------------------------------------------
struct NpcEntry {
    ue::UObject* actor = nullptr;
    std::string tail;      // the cross-client key (SPN_ALIAS applied)
    std::string cls;       // GetFullName(GetClass())
    Vec3 pos;
    double distSelf = 0.0;
    double distPeer = 0.0;
    bool owned = false;
};

struct NpcWire {
    std::string key;
    Vec3 pos;
    double hp = 1.0;
    bool dead = false;
    Vec3 vel;
    double yaw = 0.0;
    std::string montage;   // asset path, "" = none
};

struct PeerClaim {
    std::string key;
    Vec3 pos;
};

struct MirrorEntry {
    ue::UObject* actor = nullptr;
    int savedMode = 1;
    std::string mont;      // last montage path we played
    double seen = 0.0;     // net::Now() of the last drive
    bool aiOff = false;
};

struct CombatState {
    std::vector<NamedActor> hcCache;             // fullname(owner) -> SpartaHealthComponent
    StrNumMap hpSeen;                            // tail -> lowest normalized health seen
    StrSet deadTails;
    std::vector<PeerClaim> peerClaims;           // PEER_CLAIMS
    StrSet ownedTails;                           // OWNED_TAILS
    std::vector<NpcEntry> npcLast;               // NPC_LAST (sorted)
    int npcOwned = 0, npcMirrored = 0;
    std::vector<MirrorEntry> mirrors;            // MIR
    StrSet mirrorSkip;                           // MIR_SKIP
    int mirTaken = 0, mirReleased = 0, mirMontages = 0;
    StrSet mirroredOnce;
    int npcTicks = 0, npcGood = 0;
    bool npcVerdictDone = false, npcMoved = false;
    std::vector<NamedActor> npcFirst;            // tail -> first position seen

    // spawn replay (main.lua:3817)
    std::vector<NamedActor> spawners;            // tail -> UnitSpawner
    StrStrMap spawnAlias;                        // enemy tail -> "SP_<spawner tail>"  (main.lua:636)
    StrNumMap spawnReq;                           // key -> last request time
    int spawnOk = 0, spawnAsked = 0;
    StrSet spawnDone;
    double spawnListAt = 0.0;

    // ai / damage / perf (main.lua:3818-3820)
    int aiRedirects = 0;
    StrSet aiLogged;
    double pdSent = 0.0;
    bool hasPdGot = false;
    double pdGot = 0.0;
    int pdApplied = 0;
    double ftNearSum = 0.0, ftFarSum = 0.0;
    int ftNearN = 0, ftFarN = 0;
    int enemyTrace = 0;
};

// ---------------------------------------------------------------------------
// owner: coop_world  (main.lua:2197-2602, 3344-3737)
// ---------------------------------------------------------------------------
struct WsKind {
    const char* cls;            // e.g. "BP_HubDoor_C"
    const char* fields[4];      // up to 4 property names, nullptr terminated
    bool isActor = false;       // the value lives on the actor itself (BP_HubDoor_C)
    bool isDoor = false;        // replay through OpenDoor()
};

struct WsRow {
    std::string key;            // 8 hex chars (FNV-1a of the owner name)
    bool v = false;             // known value
    int kind = 0;               // index into the WS_KINDS table
    int field = 0;              // field index inside that kind
    ue::UObject* obj = nullptr;
    std::string owner;          // actor name under PersistentLevel
    double born = 0.0;          // net::Now() when first seen -> the 8 s settle gate
    std::string fn;             // GetFullName() of obj (re-identification)
};

struct LedgerState {
    StrSet keys;
    StrSet fromLedger;
    StrSet counted;
    std::string file;           // ws_ledger_<slot>.txt
    std::string state;          // "wait" | "ok" | "dropped"
    int good = 0, bad = 0;
    bool dirty = false;
    double savedAt = 0.0;
    bool loaded = false;
    double loadedAt = 0.0;
    bool verdict = false;
};

struct LiftEntry {
    ue::UObject* obj = nullptr;
    std::string name;
    int seq = 0;
    int floor = 0;
    bool active = false;
    bool hasLast = false;
    bool last = false;
    bool hasFloor = false;
};

struct PeerLift {
    std::string name;
    int seq = 0;
    int floor = 0;
    bool active = false;
};

struct ChestSnapshot {
    std::string path;
    int count = 0;
};

struct ChestEvent {
    std::string name;
    int seq = 0;
    int count = 0;
    double at = 0.0;
    bool done = false;
    std::string loot;           // "path=n;path=m"
    int baseIndex = 0;          // index into the snapshot ring used as "before"
};

struct ChestEntry {
    ue::UObject* obj = nullptr;
    double born = 0.0;
    bool hasUsed = false;
    int used = 0;
};

struct LootQueueItem {
    std::string path;
    int n = 0;
    int tries = 0;
    std::string chest;
};

struct WorldState {
    std::vector<WsRow> seen;
    std::vector<std::string> trueList;   // discovery order
    StrSet isTrue;
    StrNumMap recent;                    // key -> net::Now() of the local false->true flip
    StrIntMap peer;                      // key -> 0 live / 1 base / 2 ledger  (SmallEnum)
    StrSet done;
    int rot = 1;
    int kindIdx = 0;
    std::string payload;                 // the ",ws":[...] string
    int scans = 0;
    int appliedLive = 0, appliedBase = 0, appliedLedger = 0, appliedFail = 0;
    StrIntMap liveOk;                    // key -> 0 pending / 1 confirmed
    StrIntMap diag;                      // key -> last logged bool
    bool verdict = false;
    LedgerState ledger;

    // lifts
    std::vector<LiftEntry> lifts;
    std::vector<PeerLift> liftPeer;
    StrIntMap liftPeerSeen;
    StrIntMap liftWant;                  // name -> target floor
    StrNumMap liftReplay;
    int liftApplied = 0, liftFail = 0;
    std::string liftPayload;
    double liftPrimed = 0.0;
    bool liftBased = false;
    bool liftWatch = false;
    std::string liftWatchName;
    double liftWatchZ0 = 0.0;
    int liftWatchFloor = 0;
    bool liftVerdict = false;

    // chests
    std::vector<ChestEntry> chests;
    std::vector<std::vector<ChestSnapshot>> chestSnaps;   // ring of <= 6 inventory snapshots
    std::vector<ChestEvent> chestEvents;
    int chestSeq = 0;
    StrIntMap chestExt;
    StrIntMap chestLidCount;
    StrSet chestDone;                    // "<name>#<seq>"
    std::vector<LootQueueItem> chestQueue;
    int chestApplied = 0, chestLoot = 0, chestFail = 0;
    std::string chestPayload;
    bool chestVerdict = false;
    ue::UObject* itemMgrClass = nullptr;
    bool chestWatch = false;
    std::string chestWatchName;
    double chestWatchAt = 0.0;
    bool chestLastGrantValid = false;
    std::string chestLastGrantPath, chestLastGrantChest;
    int chestLastGrantBefore = 0, chestLastGrantAfter = 0;
    int chestTestTmp = 0;
};

// ---------------------------------------------------------------------------
// owner: coop_progress  (main.lua:2603-3343)
// ---------------------------------------------------------------------------
struct WpSet {
    StrIntMap mine;                       // name -> value (presence keys use -1 for removed)
    std::vector<std::string> order;       // discovery order for the rotating window
    StrNumMap recent;                     // name -> net::Now() of a local change
    StrIntMap peer;                       // name -> peer value
    StrIntMap peerLive;                   // name -> 1 when the peer flagged it live
    StrSet done;
    StrIntMap tried;                      // name -> last value tried (quest stage/counter)
    StrNumMap behindSince;                // quest stage: first time we were behind
    StrIntMap heard;                      // "<name>@<stage>" -> 1 (objectives already heard)
    StrIntMap stageHeard;                 // quest -> objectives heard
    StrSet removed;
    StrIntMap ok;                         // name -> how ("live"/"base"/"add"/"remove"/...)
    // Apply history as a bitmask instead of the Lua "-+"/"+-" string:
    //   bit0 = a "+" (add/apply) happened, bit1 = a "-" (remove) happened.
    // wpJudge's "removed and restored" test becomes (histMap.Get(name) & 3) == 3.
    StrIntMap histMap;
    int rot = 1;
    int window = 12;
    int applied = 0, fail = 0;
    double firstAt = 0.0;
    bool primed = false;
    double lastAt = 0.0;
    std::string payload;
};

struct ProgressState {
    WpSet wb;    // store B  ("wb")
    WpSet wq;    // quests   ("wq")
    WpSet wd;    // layers   ("wd")
    ue::UObject* helper = nullptr;   // /Script/Sparta.Default__SpartaWorldStateHelper
    ue::UObject* sub = nullptr;      // SpartaSaveSubSystem
    ue::UObject* qlog = nullptr;     // BPC_QuestLogComponent_C
    ue::UObject* dlm = nullptr;      // DataLayerManager
    bool testActive = false;
    double testAt = 0.0;
    std::string testTag, testLayer;
    bool testFinished = false;
    bool qtest = false;
    double qtestAt = 0.0;
    std::string qtestId;
    bool qtestFinished = false;
    bool q2test = false;
    double q2testAt = 0.0;
    std::string q2testId, q2testNpc;
    int q2testRel = 0;
    int q2testStep = 0;
    bool wpVerdict = false;
    bool qsVerdict = false;
    bool qs2Verdict = false;
    bool qs2SeenSet = false;
    double qs2Seen = 0.0;
    int tickN = 0;
};

// [R5-DEATH] begin
// ===========================================================================
// 12b. coop_death -- co-op player death (the three-case rule)
//
// The RULES live in coop_policy.h (pure, engine-free, unit-tested).  This block is
// only the state the engine half needs; the decisions are never re-implemented
// here.
// ===========================================================================

// The [Rules] values, from coop\MS2Coop.ini.  The HOST's copy is authoritative
// and is published on the wire ("rul"); a guest uses what arrives.
struct DeathRules {
    int  debuffs          = 1;    // death_debuffs
    int  maxStacks        = 5;    // debuff_max_stacks
    int  healthPct        = 10;   // debuff_health_pct   (max health per stack)
    int  shieldPct        = 10;   // debuff_shield_pct   (max SHELL health per stack)
    int  delayS           = 10;   // respawn_delay_s
    int  delayPerStackS   = 5;    // respawn_delay_per_stack_s
    int  payPenalty       = 1;    // pay_death_penalty
    int  bossHold         = 1;    // boss_hold (the case-3 hold; 0 = respawn instead)

    void Read(bool fromIni);           // defaults, then the ini on top
    std::string Serialize() const;     // "[ms,hp,sh,d,dps,pp]" for the "rul" key
    bool Parse(const std::string& body);
};

// The wire status word of one machine ("lv"):
//   bits 0-2 liveness, bits 4-5 party case, bits 8-15 death serial (mod 256).
struct DeathState {
    bool off = false;                 // DEATH_OFF.txt -- vanilla death everywhere
    bool started = false;

    // --- identity of what we are watching ---
    ue::UObject* pawn = nullptr;      // the player pawn the state belongs to
    ue::UObject* healthComp = nullptr; // its USpartaHealthComponent
    ue::UObject* pc = nullptr;        // the player controller

    // --- my own liveness ---
    int selfAlive = 1;                // 1 = up, 0 = down (a pending death here)
    int liveness = 1;                 // the value published in "lv" (bits 0-2)
    int partyCase = 0;                // 0 unknown, 1 wipe, 2 respawn, 3 hold
    int victimCase = 0;               // the case computed at the moment of death
    unsigned serial = 0;              // death serial (mod 256 on the wire)
    int deathStateRaw = 0;            // ESpartaDeathState as read (0/1/2)
    int deathMix = 0;                 // BP_PlayerController_C.bDeathMixActive (the real flag)

    // --- the pending death ---
    bool pending = false;
    int pendingSignal = 0;            // a death EVENT fired (the hook sets it; Tick15 decides)
    double lastHealthLogAt = 0.0;     // the pending-state health table's clock
    double dieFallbackAt = 0.0;       // "die" asked S_KillMe; if nothing happened, StartDeath
    double deathAt = 0.0;             // net::Now() of the death
    double respawnAt = 0.0;           // net::Now() when the co-op respawn is due
    double lastRecaseAt = 0.0;        // 4 Hz re-decision clock
    double lastRespawnTry = 0.0;
    int    respawnTries = 0;
    int    respawnStep = 0;           // the ladder: 0 none, 1 revive, 2 restart, 3 teleport
    Vec3   deathPos;                  // the frozen death position
    bool   hasDeathPos = false;

    // --- stacks ---
    int stacks = 0;
    CoopDebuffVal maxHealth;          // the debuff held on GetMaxHealth
    CoopDebuffVal maxShell;           // ... and on GetMaxShellHealth
    double lastDebuffAt = 0.0;

    // --- boss fight (case 3) ---
    int bossFake = 0;                 // "bossfake 0|1" -- forced, for tests
    int bossFound = 0;                // from the live objects, this machine
    double lastBossAt = 0.0;
    int spectating = 0;               // the view target is the partner's body
    ue::UObject* viewTarget = nullptr;

    // --- the partner ---
    int peerSeen = 0;                 // seen at least once this session
    int peerLiveness = 0;             // its published "lv" liveness bits
    int peerCase = 0;
    unsigned peerSerial = 0;
    double peerAt = 0.0;              // net::Now() of the last liveness word
    int peerBoss = 0;                 // its "bs"
    int peerStacks = 0;               // its "dstk"

    // --- the wipe / host coordination ---
    int hostResetSerial = 0;          // my "wr": bumped when I perform a reset
    int peerResetSerial = 0;          // the peer's "wr"
    int hostUp = 0;                   // guest: the host's liveness says ALIVE
    int followerRespawn = 0;          // guest: a wipe-follow respawn is armed

    // --- rules, and where they came from ---
    DeathRules rules;                 // the values in force (the host's, in a session)
    int rulesFromHost = 0;            // 1 when `rules` came from the wire

    // --- suppression bookkeeping / logging ---
    unsigned publishedWord = 0xFFFFFFFFu;  // the last "lv" word (log on change only)
    int suppressed = 0;               // vetoed reset/reload calls
    int worldChanges = 0;             // world resets seen (sync.txt worldresets:)
    int debuffVerified = 0;           // the one read-back check of the debuff write
    double lastLoggedAt = 0.0;
    double missReviveAt = 0.0;        // the MISSING REVIVE watchdog's clock
    int hookResolved = 0;             // how many veto hooks are installed
};

// Reads [Rules] (and writes the commented default file when it is absent), the
// DEATH_OFF.txt kill switch, and installs the reset-veto hooks.
namespace death {

void Start();

// 15 Hz: detection, the party case, the respawn ladder, hold/spectate.  Cheap
// when nothing is happening (one property read).
void Tick15();

// 2 Hz: the death stack's debuff on max health / max shell health, re-based when
// the game sets its own value.
void Tick2();

// Called from net::ResetAll on a world change (a map load, a death reload).
void Reset();

// Appends `,"lv":..,"bs":..,"dstk":..,"wr":..,"rul":[...]` to the publish line.
void Publish(std::string* line);

// Parses the death keys out of the peer's packet (the raw line).
void OnPeerLine(const std::string& line);

// The "death=" sync.txt line.
std::string Status();

// cmd.txt verbs: die / bossfake <0|1> / dstate / deathprobe <s> [/ <patterns>].
// Returns true when the verb was one of ours.
bool OnCommand(const std::string& cmd, const std::string& rest);

} // namespace death
// [R5-DEATH] end

// ---------------------------------------------------------------------------
// owner: coop_boss  [R7-BOSS]  (gap G7 of docs/HANDOFF-REMAINING-SYNC.md)
// ---------------------------------------------------------------------------

// One boss component seen by the local scan.  `comp` is UBPC_BossAI_C; `actor` is
// its owner character (GetOwner), which is what the enemy health mirror keys on.
struct BossEntry {
    ue::UObject* comp = nullptr;
    ue::UObject* actor = nullptr;
    std::string key;       // net::Tail(actor) -- the SAME key the nhp mirror uses
    std::string cls;       // class short name of the owner ("BP_DwarfBoss_C")
    int phase = 1;         // 1 / 2 / 3, derived from the two bTriggered*Phase flags
    int lastPhase = 1;     // the phase this module last accounted for (publish/apply)
    double hp = -1.0;      // HealthNorm of the actor (<0 = unreadable)
    double t2 = 0.0;       // SecondPhaseHealthPercent, normalised to 0..1
    double t3 = 0.0;       // ThirdPhaseHealthPercent, normalised to 0..1
    bool aggro = false;    // bIsAggroed
};

// One phase event on its way out (or replayed for a couple of seconds in case a
// packet was lost -- the seq makes a repeated packet harmless).
struct BossEvent {
    std::string key;
    std::string cls;
    int phase = 1;
    int seq = 0;
    double at = 0.0;       // net::Now() when first published
};

// One "the peer says phase N" that the local copy has not reached yet.  Retried
// for a few seconds because the phase function is gated on health, and the health
// mirror lands on its own schedule.
struct BossRetry {
    std::string key;
    std::string cls;       // the peer's class name: the class fallback needs it too
    int phase = 0;
    int tries = 0;
    double nextAt = 0.0;
};

struct BossState {
    std::vector<BossEntry> bosses;          // refreshed by the scan
    std::vector<BossEvent> pending;         // events to publish (<= kBossEventHold s old)
    std::vector<BossRetry> retries;         // peer phases not applied yet
    StrIntMap appliedPhase;                 // key -> phase applied from the peer
    StrIntMap seenSeq;                      // key -> last peer seq taken
    StrIntMap localSeq;                     // key -> last seq THIS client handed out
    double lastScanAt = -1.0e9;
    bool flipSeen = false;                  // a phase hook fired since the last scan
    // counters (sync.txt / the BOSSPHASE verdict)
    int scanned = 0;                        // scan passes
    int hookFires = 0;                      // phase-hook hits
    int published = 0;                      // events put on the wire
    int applied = 0;                        // peer phases applied locally
    int peerEvents = 0;                     // peer phase events accepted (seq/echo filtered)
    int refused = 0;                        // the game's own function said no
    int missed = 0;                         // a peer phase with no local boss
    int echoes = 0;                         // local flips attributed to our own apply
    int hookFail = 0;                       // HookPost could not resolve (logged once)
};

namespace boss {

// 15 Hz (from net::Publish): rescans when a hook fired or a second elapsed, turns
// a flag change into an event, and drains the event list into `,"bsp":[...]`.
void Serialize(std::string* out);

// Parses the peer's "bsp" section and drives the game's own phase function on the
// local copy of that boss.  Called from net's apply dispatch after combat.
void OnPeerLine(const std::string& line);

// Called from net::ResetAll on a world change: every cached component is dropped.
void Reset();

std::string Status();      // the "boss=" sync.txt line

// cmd.txt verbs: bossphase dump|force <2|3>|sim <2|3>|spawn [class].
// Returns true when the verb was one of ours.
bool OnCommand(const std::string& cmd, const std::string& rest);

} // namespace boss
// [R7-BOSS] end

// ===========================================================================
// 6b. [R7-DIALOG] dialogue + cinematics (native\src\coop_dialog.cpp)
//
// (Numbered 6b rather than 16 because it MUST precede State below: State holds a
// dialog::State.  zone/body below State keep their state file-static for exactly
// that reason; this lane's state is also read by coop_puppet and coop_combat.)
//
// The player is BUSY when the game has taken its input away: a conversation with
// an NPC, or a level-sequence cutscene.  Neither is a state this mod can move to
// the peer -- the conversation is a UI and an audio stream, the cutscene is a
// per-map authored asset -- so what is synced is (a) the FACT that the player is
// busy, so the peer's copy of that body stops behaving like a fighting player,
// and (b) the dialogue OUTCOME the game persists (the quest log's global
// PlayerChoice tag; narrative tags and quest rows already travel in "wq").
//
// Wire (own top-level keys, appended LAST so no existing key moves):
//   ,"dl":[<active>,<seq>,<node>,<opts>,"<dialogueName>"]   the local dialogue
//   ,"ci":[<active>,<seq>,"<sequenceName>"]                the local cinematic
//   ,"ct":[<outSeq>,"<PlayerChoice>"]                      the dialogue outcome
// <seq> is bumped on every transition, so a lost or repeated packet is harmless;
// a section is only emitted while it is (or was, for 4 s) non-zero.
//
// Receiver: PeerLocked() makes the puppet stand in its idle clip instead of a
// montage (a friend whose own client is frozen mid-conversation must not look
// frozen here), keeps enemies from being retargeted onto a body that cannot
// fight, and stops charging damage to a player who cannot dodge.
//
// Kill switches: DIALOG_OFF.txt (no "dl"/"ct", no puppet effect) and
// CINE_OFF.txt (no "ci", no puppet effect).
// ===========================================================================
// [R7-DIALOG] begin
namespace dialog {

// One side's "the player is busy" state as it travelled over the wire.
struct LockState {
    int active = 0;             // 1 while the side is in a dialogue / cinematic
    int kind = 0;               // 0 none, 1 dialogue, 2 cinematic
    int seq = 0;                // transition counter (loss-safe freshness)
    std::string name;           // dialogue asset / level sequence, short name
    int node = 0;               // UDlgContext::GetActiveNodeIndex (dialogue only)
    int opts = 0;               // UDlgContext::GetOptionsNum    (dialogue only)
    int elapsedMs = 0;          // cinematic: how far the initiator is (late join / seek)
    double at = 0.0;            // net::Now() of the sample / of the receipt
};

struct State {
    LockState mine;             // what THIS client is doing
    LockState peer;             // what the peer last told us
    bool peerKnown = false;     // a "dl"/"ci" arrived at least once
    // the dialogue outcome (the quest log's global story tag)
    std::string choice;         // what THIS client's log says now
    std::string peerChoice;     // what the peer published
    int outSeq = 0;             // bumped on every local PlayerChoice change
    int peerOutSeq = -1;        // last applied
    int applies = 0;            // outcome writes that took
    int fail = 0;
    // counters the DIALOG / CINE verdicts read
    int idleForced = 0;         // samples the puppet was held on the idle clip
    int pdSkips = 0;            // PuppetDamage calls suppressed while the peer was busy
    int aiSkips = 0;            // AiTargetPeer retargets suppressed
    int episodes = 0;           // busies this client SAW on the peer
    int localEpisodes = 0;      // busies this client went INTO
    std::string peerPath;       // the peer's dialogue ASSET path, so a LISTENER can read
                                // the very same node (line text + voice asset) from it
    std::string peerSpeaker;    // the line ITSELF on the wire ("dl" tokens 6,7,8), so a
    std::string peerLine;       // listener never depends on having the asset loaded
    std::string peerVoice;
};

void Start();                              // kill switches, detector caches
void Tick2();                              // 2 Hz: detect, sample, verdicts
void OnPeerLine(const std::string& line);  // "dl", "ci", "ct"
void Reset();                              // world change: drop engine caches
void Serialize(std::string* out);          // the three sections, appended last

std::string Status();                      // the "dlg=" and "cine=" lines

bool LocalLocked();                        // MY player is in a dialogue/cinematic
bool PeerLocked();                         // the PEER's is
bool PeerLockedFresh();                    // ...and the state is recent (< 4 s)
double PeerAge();                          // seconds since the peer's last lock sample
int  PeerKind();                           // 0 none, 1 dialogue, 2 cinematic
void NoteSkip(int what);                   // 0 = AI retarget, 1 = puppet damage
void NoteApplyAt();                        // ApplyPeer ran (PeerKeepAlive's throttle)
void PeerKeepAlive();                      // 15 Hz, never skipped: apply the peer's state

// Test hooks (cmd.txt).  dlgfake / cinefake are STATE INJECTORS in the same
// spirit as zonefake: they put this client into the "busy" state the wire and the
// receiver are built for, so the DIALOG / CINE verdicts can run without a human
// clicking an NPC.  dlgstart / dlgstop drive the GAME's own dialogue path
// (ABP_DialogueActor_C's BPC_DialogueInstigator_C::StartDialogue), which is what a
// lab run uses to prove the detector sees a real conversation.
void CmdFake(const std::string& rest);      // dlgfake  on|off [name]
void CmdCineFake(const std::string& rest);  // cinefake on|off [name]
void CmdDump(const std::string& rest);      // dlgdump
void CmdStart(const std::string& rest);     // dlgstart [npcClassSubstring]
void CmdStop(const std::string& rest);      // dlgstop
void CmdChoice(const std::string& rest);    // dlgchoice <tag>  (outcome test)
void CmdSelfTest(const std::string& rest);  // dlgselftest (the wire codec, no engine)
void CmdVote(const std::string& rest);      // dlgvote <option>  (1-based; the vote channel)
void CmdVoteSim(const std::string& rest);   // dlgvotesim (the resolution cases)
void CmdCinePlay(const std::string& rest);  // cineplay [sequenceName] -- a REAL local cutscene
void CmdCineStop(const std::string& rest);  // cinestop
void CmdCineHold(const std::string& rest);  // cinehold <seconds> [name] -- test hold
void CmdPdReset(const std::string& rest);   // pdreset -- puppet body back to full
void CmdHoldClear(const std::string& rest); // holdclear -- drop the local hold

} // namespace dialog
// [R7-DIALOG] end

// ===========================================================================
// 6b. coop_drops -- [R7-DROPS] G6: enemy drops shared between the two players
// ===========================================================================
// The owner of an enemy (the client whose copy of it died) is the ONLY one that
// lets the game roll the loot.  It reads what the game rolled and publishes it;
// the other client suppresses its own roll for that death and grants a copy to
// its own player.  Currency/inventory stay per player: a copy is granted, totals
// are never mirrored.
struct DropItem {
    std::string path;   // item definition asset path ("/Game/.../ID_Gold.ID_Gold_C")
    int count = 0;
    double weight = 0.0;   // [R7c] the table entry's own chance (or weight), before rolling
};

struct DropEvent {
    long long seq = 0;              // our per-sender monotonic id (dedupe on the peer)
    std::string key;                // the enemy's cross-client key (a tail)
    int gold = 0;
    int gloom = 0;
    std::vector<DropItem> items;    // item definitions the death dropped
    double born = 0.0;              // net::Now() when the roll was read
    bool sent = false;              // at least one packet carried it
};

// A death that OUR side caused by copying the peer's enemy: if the game's drop
// code runs for one of these, it must be vetoed (the peer rolled that loot).
struct DropVeto {
    ue::UObject* actor = nullptr;
    std::string tail;
    double until = 0.0;             // net::Now() + kDropVetoSeconds
    int fired = 0;                  // how many drop attempts were vetoed
};

// [R7c] One death whose loot the OWNER still has to roll.  A QUEUE, not a slot: a
// fight kills several enemies within a second, and each death owes its own roll.
// The roll happens kDropFallbackSeconds after the death so the game's own ability
// (which may fire late, after a long death animation) wins whenever it runs.
struct PendingRoll {
    ue::UObject* actor = nullptr;
    std::string tail;               // the cross-client key
    double at = 0.0;                // net::Now() when the fallback may roll
    int gold = 0;                   // captured CharacterDeathData
    int gloom = 0;
    float chance = 0.0f;
    std::vector<DropItem> items;    // ItemDropTable / AdditionalDropItems candidates
};


struct DropsState {
    // --- outgoing (we rolled it) ---
    long long outSeq = 0;           // last seq handed out
    std::vector<DropEvent> out;     // still being retransmitted (bounded by age)
    // --- incoming (the peer rolled it) ---
    long long lastPeerSeq = 0;      // high-water mark: a repeat is harmless
    int dupSkipped = 0;
    // --- veto (the death was a mirrored death) ---
    std::vector<DropVeto> veto;
    // --- counters for sync.txt / the verdict ---
    int calls = 0;                  // un-vetoed AI deaths that ran the drop code
    int rolled = 0;                 // local deaths whose roll we read
    int sentEvents = 0;             // events put on the wire at least once
    int gotEvents = 0;              // peer events applied
    int grantedItems = 0;           // item grants that succeeded
    int grantedGold = 0;            // gold granted to our player (sum)
    int grantedGloom = 0;           // gloom granted to our player (sum)
    int vetoed = 0;                 // drop attempts blocked on mirrored deaths
    int hooks = -1;                 // installed drop hooks (logged on change)
    double hookRetryAt = 0.0;       // next hook-install attempt
    // --- the measurement probe (dropsprobe) ---
    bool probe = false;
    double probeUntil = 0.0;
    int  probeFires = 0;
    std::string lastKey;
    int lastGold = 0, lastGloom = 0;
    int lastWallet = -1;            // last logged gold+gloom balance
    // What the death actually SPAWNED near the corpse (currency bag? item
    // pickup?) -- sampled 1.5 s after the roll, once per probe death.
    bool probeScan = false;
    double probeScanAt = 0.0;
    Vec3 probePos;
    int probeScans = 0;
    // [R7b] a real player hit was just delivered: watch the target's health for a
    // moment and say whether the damage landed and the enemy died.
    bool pendingHit = false;
    std::string pendingTail;
    ue::UObject* pendingActor = nullptr;
    double pendingHp0 = 0.0;
    double pendingAt = 0.0;
    // [R7b] an OWNER death whose loot roll has not been read yet: if the game's
    // death ability has not rolled within ~3 s, the owner rolls from the enemy's own
    // CharacterDeathData (the ability is only activated from the damage/ASC path,
    // which a health write never reaches -- see coop_drops.cpp).
    //
    // [R7c] it is a QUEUE (one entry per enemy) and it is deduped BOTH ways so a
    // death can never be granted twice:
    //   * rolledByFallback: the owner already rolled for that key, so a LATE game
    //     roll for it is vetoed by the existing PRE-SKIP hook;
    //   * rolledByGame: the game rolled for that key, so the fallback stands down.
    std::vector<PendingRoll> pendingRolls;   // cap kMaxPendingRolls
    StrSet rolledByFallback;                 // keys the data roll granted
    StrSet rolledByGame;                     // keys the game's ability granted
    StrNumMap rolledAt;                      // key -> net::Now() of either roll
    double lastClaimAt = 0.0;                // rate limit for the provisional file only
    int alreadyRolledVetoed = 0;             // late game rolls blocked after a fallback
    int fallbackCancelled = 0;               // fallbacks that stood down for the game
    int queuedRolls = 0;                     // deaths queued for the fallback
};

namespace drops {

// 2 Hz: hook install retry, retransmit expiry, the probe's wallet sampler.
void Tick2();

// Called from net::ResetAll on a world change (every pointer here dies with it).
void Reset();

// Appends `,"drp":[[<seq>,"<key>",<gold>,<gloom>,"<path>@<n>|..."],...]` -- nothing
// when there is no unacknowledged event (the steady-state packet stays identical).
void Publish(std::string* line);

// Parses the peer's "drp" section and grants what the peer rolled.
void OnPeerLine(const std::string& line);

// Called by coop_combat RIGHT BEFORE it kills a local copy of the peer's enemy
// (StartDeath on a mirrored death): from then on that actor's own drop roll is
// vetoed, because the peer rolled the loot already.
void MarkMirrorDeath(ue::UObject* actor, const char* why);

// The "drops=" sync.txt line.
std::string Status();

// cmd.txt verbs: dropsprobe [secs] / dropswallet / dropsgrant <gold> <gloom> /
// dropskill <tail|nearest> / dropsdump.  Returns true when the verb was ours.
bool OnCommand(const std::string& cmd, const std::string& rest);

} // namespace drops
// [R7-DROPS] end

// ===========================================================================
// 6c. [R8-MONEY] the player's OWN death drop -- the lost-Gloom spoils
// ===========================================================================
// The game takes the carried Gloom on death and leaves it at the death spot:
// GA_DeathSpoilsHandler_C removes the gloom (GetGloom/RemoveGloom ->
// SpartaItemManager), writes `bHasLostGloom`/`LostGloomAmount @0x578`/
// `LostGloomLocation @0x580` into BP_SpartaPlayerSaveObject_C and spawns ONE
// BP_DeathSpoils_C (`Amount @0x308`, `Recovered @0x314`) there.  Picking it up
// runs GiveGloom (S_AddGloom) + ResetSaveData.
//
// In vanilla that actor is destroyed by the world RELOAD every death performs.
// A co-op case-2 death does not reload, so this lane has to answer, with numbers:
// is the bag still there, still visible, still collectable, does it give back
// exactly the lost amount, does a SECOND death still replace the old drop, and
// does the partner stay out of it (currency is per player).
//
// Read-only except for one deliberate action: the stale-drop sweep (see
// coop_money.cpp), which is what keeps the game's "old drop replaced/lost" rule
// when there is no reload to enforce it.
struct SpoilsObs {
    std::string name;      // the actor's name (GetName) -- our key for "still there"
    Vec3 pos;
    int amount = -1;       // ABP_DeathSpoils_C.Amount  (-1 = unreadable)
    bool recovered = false;// ABP_DeathSpoils_C.Recovered
    bool hidden = false;   // bHidden
    int rendered = -1;     // WasRecentlyRendered(0.5): 1 = it WAS on screen lately
    double dSelf = 0.0;    // distance to our pawn
    double dDeath = 0.0;   // distance to the frozen death position
};

// One sample of the lost-gloom fields of the player save object.
struct LostGloomSave {
    bool has = false;      // bHasLostGloom
    int amount = -1;       // LostGloomAmount  (-1 = unreadable)
    Vec3 loc;              // LostGloomLocation
    bool locOk = false;
};

struct MoneyState {
    // --- the wallet (carried Gloom, the currency the death takes) ---
    int gloomNow = -1;         // last sample, -1 = unreadable
    int goldNow = -1;
    int gloomBefore = -1;      // the sample in force when the death was noted
    int gloomAtDeath = -1;     // ... and the first sample after it
    int gloomAtRespawn = -1;
    int gloomAfterPickup = -1;
    double walletAt = 0.0;     // net::Now() of the last sample

    // --- the save object's lost-gloom slot ---
    LostGloomSave save;        // sampled 2 Hz while alive
    LostGloomSave prev;        // ... as it stood BEFORE this death
    // MEASURED (run 1): the sample that lands on the same tick as the death already
    // carries the ability's NEW value, so "prev == the death's own write" made the
    // stale-drop sweep think the slot had not changed.  The two-deep ring is what
    // makes "before" really mean before: NoteDeath takes the OLDER of the last two
    // samples, which is always older than the death by one sample interval.
    LostGloomSave ring[2];
    double ringAt[2] = {0.0, 0.0};
    int ringNext = 0;
    int lostAmount = -1;       // the amount this death lost (the save's, after it)
    bool saveChanged = false;  // the death wrote a new amount / location

    // --- the death being followed ---
    unsigned serial = 0;
    double deathAt = 0.0;
    Vec3 deathSpot;            // where the corpse was -- the drop's place, and what
    bool hasDeathSpot = false; // the mod writes into the slot when the game did not
    double sweepAt = 0.0;      // when the stale-drop sweep is due
    bool watch = false;
    double watchUntil = 0.0;

    // --- the spoils actors in this world ---
    std::vector<SpoilsObs> now;   // the latest scan
    StrSet seen;                  // names ever seen (a new name = a new drop)
    int scans = 0;
    int appeared = 0;
    int gone = 0;
    int staleRemoved = 0;         // stale drops WE destroyed (the (c) rule)
    int swept = 0;
    int restores = 0;             // drops WE put back from the save slot
    double restoreAt = 0.0;       // a post-load restore check is due

    // --- collection ---
    int pickupStage = 0;          // 0 none, 1 handler path sent, 2 direct event sent
    double pickupAt = 0.0;
    std::string pickupName;
    int pickupAmount = -1;        // the bag's Amount
    int pickupWallet = -1;        // the wallet just before
    int collected = 0;            // collections confirmed
    int returned = 0;             // the sum the pickups gave back
    int lastReturned = -1;        // the last confirmed delta
    int pickupFail = 0;

    // --- the probe / logging ---
    bool probe = false;
    double probeUntil = 0.0;
    int lastCount = -1;           // the last logged spoils count
    int lastWallet = -1;
    bool off = false;             // MONEY_OFF.txt
};

namespace money {

// 2 Hz: the wallet/save sampler, the spoils scan, the stale-drop sweep and the
// collection check.  Cheap while nothing is happening (one FindAllOf every 5 s).
void Tick2();

// Called from net::ResetAll on a world change (every actor pointer here dies).
void Reset();

// Called by coop_death when its one detection path fires / when the player is up
// again.  Never writes anything but this lane's own counters.
void NoteDeath(ue::UObject* pawn);
void NoteRespawn();

// The "money=" sync.txt line: what is on the ground, what was lost, what came back.
std::string Status();

// cmd.txt verbs: moneyprobe [secs] / moneydump / moneyget [radius] / moneyview.
// Returns true when the verb was one of ours.
bool OnCommand(const std::string& cmd, const std::string& rest);

} // namespace money
// [R8-MONEY] end
// ===========================================================================
// 7. the one global
// ===========================================================================

struct State {
    Toggles toggles;
    LocalState local;
    PeerState peer;
    PuppetState puppet;
    AppearState appear;
    CombatState combat;
    WorldState world;
    ProgressState progress;
    // [R5-DEATH] begin
    DeathState death;
    // [R5-DEATH] end
    // [R7-BOSS] begin
    BossState boss;
    // [R7-BOSS] end
    // [R7-DIALOG] begin
    dialog::State dialog;
    // [R7-DIALOG] end
    // [R7-DROPS] begin
    DropsState drops;
    // [R7-DROPS] end
    // [R8-MONEY] begin
    MoneyState money;
    // [R8-MONEY] end

    // guards (main.lua:4000-4003)
    int64_t localHoldUntil = 0;    // wall seconds
    double worldSettleUntil = 0.0; // net::Now()
};

State& state();          // the single instance, defined in coop_net.cpp
const Toggles& toggles();

// Called once by the proxy DLL's worker thread after ue::Init() has succeeded:
// brings up the base layer (net::Start) and registers the one per-frame tick.
// Defined in coop_main.cpp.  Never throws; safe to call from a non-game thread
// because nothing here touches a UObject (the tick callbacks do, and those run on
// the game thread).
void Start();

// Called by every module's Reset(); net::Start() and net::OnWorldChanged() call
// them all. Never throws.
void ResetAll();

// ===========================================================================
// 8. coop_net -- session, IO, wire, commands, status, loops
//    owner of: Toggles, PeerState, Phase, file layout, all frozen strings
// ===========================================================================

class Tick {
public:
    using Fn = std::function<void(double)>;

    // Register a callback at a fixed rate. Called once from net::Start().
    void Every(double hz, const char* label, Fn fn);

    // Drive the router; called from ue::OnTick with the frame dt.
    // Each due callback runs inside net::Step(label, Guard::None, fn).
    void Frame(double dt);

    void Reset();

private:
    struct Slot {
        double hz = 0.0;
        double acc = 0.0;
        const char* label = nullptr;
        Fn fn;
    };
    std::vector<Slot> slots_;
};

Tick& ticks();

namespace net {

// --- lifecycle -------------------------------------------------------------
// Resolves the coop dir (coop_dir.txt, three legacy probes, module dir),
// reads playerid.txt / role.txt, loads Toggles, deletes stale out_*/peer_*,
// writes "=== CoopMod start (me=%s dir=%s role=%s) ===" and result.txt=RUNNING.
void Start();

// Called by ue::OnTick. Dispatches ticks(), then the 15/2/1 Hz work.
void Frame(double dt);

// Called when the World object changes (map load / death reload): every module's
// caches are dropped before anything runs again.
void OnWorldChanged();

// --- the three loops (implemented by net, calling the modules) -------------
void Publish();          // 15 Hz  main.lua:1566  sendOwn
void ApplyPeer();        // 15 Hz  main.lua:1936  applyPeer
void RunCommands();      // 2 Hz   main.lua:2080  runCommands
void WriteSyncStatus();  // 2 Hz   main.lua:3968  writeSyncStatus
void Heartbeat();        // 1 Hz   main.lua:4023  LoopAsync(1000)

// --- phase machine (the Phase enum lives in state().puppet) ----------------
void EnterPhase(Phase p);            // main.lua:498  enter()
double PhaseSeconds();               // main.lua:499  pt()

// --- guards (main.lua:3995-4017) -------------------------------------------
bool LocalHold();                    // main.lua:4003
void SetLocalHold(double secondsFromNow);
bool WorldSettle();
void SetWorldSettle(double secondsFromNow);
// Runs fn unless the guard skips it; logs "ERR <label>: ..." on exception.
// Returns true when it ran.
bool Step(const char* label, Guard guard, const std::function<void()>& fn);

// --- clock ----------------------------------------------------------------
double Now();      // GetRealTimeSeconds(world): play heads, settle windows, rate limits
int64_t Wall();    // wall-clock seconds: everything that used os.time()

// --- filesystem (relative to state().local.dir) ---------------------------
bool ReadLine(const std::string& name, std::string* out);   // first line, no '\n'
// [R7-WIRE] peer_<id>.txt is one JSON object per datagram; this merges the parts
// back into the one line every reader parses.  A single-line file (an old peer,
// or WIRE_SPLIT_OFF.txt on the sender) is returned byte-identical.
// parts/maxBytes/expected are the counters the WIRE verdict reads.
bool ReadPeerPacket(const std::string& name, std::string* out, int* parts,
                    std::size_t* maxBytes, int* expected);
// `wiretest`: the split/merge round trip, with no second client (result_wire.txt)
void WireSelfTest();
// Whole file as bytes; false when it cannot be opened.  Added for the store-A
// ledger (coop_world's CONTRACT-REQUEST 1: one key per line).
bool ReadText(const std::string& name, std::string* out);
bool WriteFile(const std::string& name, const std::string& text);
bool AppendLine(const std::string& name, const std::string& text); // flushed per line
bool FileExists(const std::string& name);
bool RemoveFile(const std::string& name);

// --- logging (main.lua:85-94) --------------------------------------------
// Writes "[HH:MM:SS][<me>] <msg>\n" to stdout and appends+flushes probe.log.
void Log(const char* fmt, ...);

// --- object helpers -------------------------------------------------------
bool Valid(ue::UObject* o);                  // ue::IsValid
std::string FullName(ue::UObject* o);        // "UObject <invalid>" / "<nil>" on null
std::string Tail(ue::UObject* o);            // main.lua:76 actorTail(FullName)
std::string NameOf(ue::UObject* o);          // GetName  (FName -> string)
std::string AssetPath(ue::UObject* o);       // first "/Game/..." token of FullName
ue::UObject* World();
ue::UObject* PC();
ue::UObject* Pawn();
Vec3 Loc(ue::UObject* o);                    // zero on failure
ue::UObject* Find(const std::string& path, bool requestLoad); // StaticFindObject + missing log
ue::UObject* EngineClass(const char* shortName); // "/Script/Engine.<name>" cache
// Console command (main.lua:176): the ONLY way to spawn the puppet.
void Console(const std::string& text);

// --- wire helpers (main.lua:1426-1451) -----------------------------------
bool Section(const std::string& line, const char* key, std::string* out); // "[...]" by depth
bool KvNum(const std::string& line, const char* key, double* out);
bool KvInt(const std::string& line, const char* key, int64_t* out);
bool KvStr(const std::string& line, const char* key, std::string* out);
bool KvHas(const std::string& line, const char* key);
bool HasRdy0(const std::string& line);       // main.lua:1942 substring test

// --- string formatting helpers (C locale, printf semantics) ---------------
std::string Fmt(const char* fmt, ...);
void AppendFmt(std::string* out, const char* fmt, ...);

} // namespace net

// ===========================================================================
// 9. coop_local
// ===========================================================================

namespace local {

void Tick15();   // refreshChars, capture transform/montage, judge, peerWatchdog, telemetry
void Tick2();    // computeMyBody, readyTick, localWeaponInfo
void Reset();

// main.lua:638-650.  Owns local.tickChars / local.tickTails (rule 1), which
// includes folding in combat's SPN_ALIAS.  combat::RefreshChars() forwards here.
void RefreshChars();

// main.lua:1564: fills the hot fields and returns false while not ready
// (net::Publish then writes the {"rdy":0} shape).
bool Capture();

// main.lua:1458-1505: the montage under the play head, resolved to a sequence.
bool CaptureMontage(MontageSig* out);

// main.lua:611: own shell mesh path + AnimBP path.
void ComputeBody();

// main.lua:1526: the PLAY_READY gate.
bool Ready();

// main.lua:706: every rendered weapon mesh -> first path, its socket, and the
// "wpns" array body (no enclosing brackets).
void WeaponInfo(std::string* wpn, std::string* wsock, std::string* wpns);

} // namespace local

// ===========================================================================
// 10. coop_puppet
// ===========================================================================

namespace puppet {

void Tick30(double dt);   // patrolTick, driveTick, travelTick
void Tick2();             // bootTick (until Sync), body mesh, streamTick
void Reset();

ue::UObject* Actor();
ue::UObject* Mesh();                    // ANIM_MESH (nullptr before the body is ready)
bool BodyReady();
bool Hidden();
void SetHidden(bool hide);              // main.lua:1640
void Setup(ue::UObject* actor);         // main.lua:545 setupPuppet (adopt)
void Boot();                            // main.lua:560 bootTick
void EnsureBody();                      // main.lua:350
void SyncBody();                        // main.lua:370
bool ApplyBodyMesh(const std::string& path); // main.lua:328 (clears the AnimBP first!)
void StreamTick();                      // main.lua:3755
void UpdateAnimation(double vx, double vy, double yaw, double now); // main.lua:454
void StartPatrol(double seconds);       // main.lua:1733 cmdPatrol
void ResetAnimationState();             // drops ANIM.state / ANIM.mode after a mesh swap

// main.lua:2142-2152 (`travel x y z [secs=60]`), command side of travelTick.
// Puts the pawn in hover mode, then walks it in a straight line over `secs`
// real seconds. Logs "travel %.0f uu over %.0fs". secs <= 0 means 60.
void StartTravel(ue::UObject* pawn, double x, double y, double z, double secs);

// main.lua:2088-2099 (`tp x y z`): teleport, hover (mode 5) and hold for 6 s
// (released by StreamTick). The "tp -> ..." log line belongs to net::RunCommands
// (it has the raw argument text); this does not log it twice.
void StartTp(ue::UObject* pawn, double x, double y, double z);

// main.lua:337: "movemode=<puppet.CharacterMovement.MovementMode>" for sync.txt.
// Returns -1 when the puppet or the property is unavailable (Lua printed nil).
int MovementMode();

// [R5-BODY] begin -- single-node playback of any animation ASSET on the puppet's
// mesh, through the same mode switch every other clip uses (PlaySeq).  Added for
// the peer death visual (a DeathsLibrary AnimSequence), which is not a montage the
// puppet state machine knows.  False when the mesh/asset is not usable yet.
bool PlayAsset(const std::string& path, bool looping);
// [R5-BODY] end

// main.lua:1543-1545: true when one of `pawn`'s own World Partition streaming
// sources reports IsStreamingCompleted() == false (readyTick's soft reason).
// `pawn` is the LOCAL pawn, not the puppet.
bool OwnStreamingIncomplete(ue::UObject* pawn);

} // namespace puppet

// ===========================================================================
// 11. coop_appear
// ===========================================================================

namespace appear {

void Tick2();     // LOOK.tick + lookSilence + apply pending weapons/look (+ lookadd probe)
void Reset();

// sender half
bool CaptureLook(std::string* out);        // main.lua:980  ("" = nothing to send yet)
void OwnLookTick();                        // main.lua:1050 (debounce, updates LOOK.mine)
const std::string& Mine();                 // LOOK.mine
std::string Hash(const std::string& s);    // main.lua:863  (djb2, must be bit-identical)

// receiver half
bool ApplyLook(const std::string& sig);    // main.lua:1113
bool ApplyWeapons(const std::string& arr); // main.lua:797  (the "wpns" body, no brackets)
void ClearWeapons();                       // main.lua:787
void SilencePuppet();                      // main.lua:1065
void ClearLook();                          // main.lua:1103

// test hook (cmd.txt: lookadd / lookdel)
bool LookAddPending();
void LookTestTick();                       // main.lua:2061
void SetLookAdded(const std::string& fullName);
void LookDel();

} // namespace appear

// ===========================================================================
// 12. coop_combat
// ===========================================================================

namespace combat {

void Tick2();    // fillHealthCache, sampleCombat, enemyTick (spawners, AI, damage, perf)
void Tick15();   // [R6-SPAWN] fast claim watch: a replayed enemy lives ~1 s
void Reset();

// The peer line, in net's call order: npc -> nhp -> dead -> pd.
void OnPeerLine(const std::string& line);

// Writers, appended by net::Publish() in this order.
void SerializeNpc(std::string* out);    // ,"npc":[[...],...]
void SerializePd(std::string* out);     // ,"pd":%.3f
void SerializeNhp(std::string* out);    // ,"nhp":[[...],...]
void SerializeDead(std::string* out);   // ,"dead":["...",...]

// Shared with coop_puppet and coop_appear.
// Returns < 0 when the health is not readable yet (main.lua:1211 returns nil).
double HealthNorm(ue::UObject* actor, bool* dead);
bool SetHealthNorm(ue::UObject* actor, double norm);   // main.lua:1227
bool StartDeath(ue::UObject* actor);                   // main.lua:1243
bool IsDeadTail(const std::string& tail);
void RefreshChars();       // main.lua:638 -- forwards to local::RefreshChars()
void FillHealthCache();    // main.lua:1194

// main.lua:2120-2122 (`kill`) / applyDead.  Kills the actor whose tail matches.
bool KillTail(const std::string& tail, const char* reason);

// [R6-WORLD] the `npctp` test verb's hand-over: stop mirroring this tail and never
// take it again, so the actor stays where the verb moved it (see coop_combat.cpp).
void MirrorClaimLocal(const std::string& tail);

std::string Status();      // the "stream=" / "enemy=" / "perf=" lines (main.lua:3957-3966)

// [R6-SPAWN] test hooks for the spawn-replay gate probe (cmd.txt):
//   spawndump [tail|nearest|all]  -- the spawner's own gate properties, one line each
//   spawnfire [tail|nearest]      -- fire that spawner's trigger by hand and watch it
void SpawnDump(const std::string& arg);
void SpawnFire(const std::string& arg);
void SpawnCall(const std::string& arg);   // spawncall <tail|nearest> <FunctionName>
void SpawnProbe(const std::string& arg);  // spawnprobe [secs] [patterns]
void SpawnSet(const std::string& arg);    // spawnset <who> <BoolProperty> <0|1>

} // namespace combat




// ===========================================================================
// 13. coop_world -- store A, ledger, lifts, chests
// ===========================================================================

// [R5-PROBE] begin
namespace probe {
void Equip(const std::string& arg);   // coop_probe.cpp, `equipprobe [apply|revert]`
void PupDump(bool self);
void PupCheck();                      // `pupcheck` -> result_pupvis.txt                       // `pupdump`
// [R9-AREA] the real weapon swing: the game's own attack ability on the ASC
void AscList(const std::string& arg); // asclist [filter]
}
// [R5-PROBE] end

// [R9-AREA] begin -- coop_realarea.cpp: find a REAL area (a boss arena) and get the
// local player into it the way the game does (a TombZone level-instance selector).
namespace area {
void Sweep(const std::string& arg);       // areasweep  [substring ...]
void Layers(const std::string& arg);      // arealayers [substring]
void Tombs(const std::string& arg);       // areatomb   [substring]
void TombGo(const std::string& arg);      // areatombgo <n|nearest>
void Go(const std::string& arg);          // areago     <n|nearest|far> [dz]
void Call(const std::string& arg);        // areacall   <n|nearest> <FunctionName>
void Attack(const std::string& arg);      // attack     [light|heavy|<AbilityClassName>]
void AttackTick();                        // 15 Hz watch: montage + ability IsActive + enemy hp
void NoteNearestEnemy(std::string* tail, double* hp, double* d);   // the watch's own target read
void EnemyTp(const std::string& arg);     // enemytp   -- stand next to the nearest live enemy
void EnemyDump(const std::string& arg);   // enemydump -- closest live enemies, class/hp/distance
void WeaponState(const std::string& arg); // weaponstate -- our weapon actors: EquipState + socket (read-only)
void Reset();                             // world change: drop the cache
}
// [R9-AREA] end

// [R8-APPEAR] begin -----------------------------------------------------------
// The peer-asset loader (coop_load.cpp).  Asset() is the only entry point the rest
// of the module uses; it returns the object when it is in memory afterwards.
namespace load {
ue::UObject* Asset(const std::string& path);
void Reset();                         // world change: drop cache + try counts
int Done();                           // successful loads (sync.txt gameloads)
int Failed();
}

// The game's own equipment writers, driven from cmd.txt (`equip ...`) so the lab
// can put a different shell/skin/mask/clothing on the LOCAL player and watch what
// the peer's puppet does.  coop_equip.cpp.
namespace equip {
void Cmd(const std::string& rest);
}
// [R8-APPEAR] end

namespace world {

void Tick2();                                  // ws scan/apply/payload/judge + lift + chest
void Reset();

void OnPeerLine(const std::string& line);      // ws, wl, wc sections

// [R6-WORLD] mark a store-A key as THIS client's own change: it is published live on
// the next packet and a peer snapshot of the same key cannot overwrite it for a few
// seconds.  The test verbs use it for the values they write themselves.
void MarkLocalWorldKey(const std::string& key);

// Writers (frozen order: ws first, then progress' wb/wq/wd, then wl, then wc).
void SerializeWs(std::string* out);
void SerializeLift(std::string* out);
void SerializeChest(std::string* out);

// FNV-1a over the owner name; THE cross-client key (main.lua:2262).
std::string Hash(const std::string& s);

// Called by coop_progress when a store-B "TimesUsed" arrives (main.lua:2742).
void ChestRefreshFromStoreB(const std::string& owner, const std::string& key, int v, bool live);

std::string Status();                          // "ws=" + "ledger=" + "lift=" + "chest="

// Test hooks (cmd.txt)
void TestWs();      // wstest
void TestLift();    // lifttest
void TestChest();   // cstest

// [R5-WORLD] begin -- store-A2 (G1 field/class additions + the G1b typed "wt"
// section).  Implemented in coop_world_ws2.cpp; the hooks below are called from
// coop_world.cpp so the shared files stay a one-line insertion each.
void Ws2Tick();                              // scan/apply/payload/judge for "wt"
void Ws2Reset();                             // drop every cached UObject*
void Ws2OnPeerLine(const std::string& line); // the ",wt":[...] section
void Ws2Serialize(std::string* out);         // appends ,"wt":[...] (nothing when off)
std::string Ws2Status();                     // the "ws2=" sync.txt line
void TestWs2();                              // wa2test
void Wa2Census();                            // wa2census  (read-only census lab run)
void Wa2FixtureReset();                      // wa2reset   (fixture reset only)
void Wa2SelfTest();                          // wa2selftest (codec unit test)
void Wa2ArrayTest();                         // wa2arraytest (the array write path)
// Reset the save-object fixtures of one class (by guid, else by class) so the
// store-A test verbs are re-runnable without deleting ws_ledger_*.txt.
// Returns how many objects were reset (-1 = the subsystem/call was unavailable).
int  ResetFixtures(const char* cls);
// [R7-WIRE] the same reset with no store-A2 gate, plus the actor-side variant the
// `fixreset` harness verb needs (chest/lift state lives on the ACTOR's save
// component GUID, not on a store-A save object the scan already knows).
int  FixtureReset(const char* cls);
int  ResetActorFixtures(const char* actorClass, int* foundOut);
void TestFixReset();                         // fixreset  (harness fixture reset)
// Called after a successful store-A apply: kinds whose flip also needs the actor's
// own action (BPSO_MovingObject_C) do it here; every other kind ignores it.
void Ws2OnApplied(int kind, int field, ue::UObject* obj, int how);
// True for a (kind, field) row that G1 added (the store-A2 verdict needs one).
bool IsNewWsRow(int kind, int field);
// "Class.Field" for a (kind, field) row (the verdict text and the census lines).
void WsRowName(int kind, int field, std::string* out);
// "Class" for a WS_KINDS index.
void WsKindName(int kind, std::string* out);
// [R5-WORLD] end

} // namespace world

// ===========================================================================
// 14. coop_progress -- store B, quest log, data layers
// ===========================================================================

namespace progress {

void Tick2();                                  // wbScan/wbApply, wqScan/wqApply, wdScan/wdApply
void Reset();

void OnPeerLine(const std::string& line);      // wb, wq, wd sections

// Writer: appends "wb", then "wq", then "wd" (main.lua:3318-3321).
void Serialize(std::string* out);

std::string Status();                          // "wp=" line (main.lua:3337)

// Test hooks (cmd.txt)
// [R7-MAPS] `mapsdump [all|ints|floats|names|bools]`: gap G2's count.  One shot,
// read-only: how many entries ActorInts/ActorBools/ActorFloats/ActorNames hold
// right now, and every float/name entry as `owner|valueName = value`.
void MapsDump(const std::string& arg);
void TestWp();      // wptest  (store B chest + narrative tag + data layer)
void TestQs();      // qstest  (add + complete a quest)
void TestQs2();     // qs2test (stage / counter / turn-in / tracked / relationship)

// [R9-QUEST] quest-stage probes (read-only except qnext/qsid's own writes):
//   qlist            -- DT_QuestData1 row names -> result_qlist.txt
//   qsid <QuestID>   -- add+track one exact quest, report its stage count
//   qnext <QuestID>  -- advance one stage (game objectives first, direct write as fallback)
//   qstage <QuestID> -- one-line snapshot of stage/stage-count/journal/tracked text
void QuestList();
void QuestStageStart(const std::string& arg);
void QuestStageNext(const std::string& arg);
void QuestStageDump(const std::string& arg);
void QuestOpenLog(const std::string& arg);   // qlog <SubTabIndex> -- game's own menu
void QuestWidgetDump();                      // qwidget -- the journal's own text rows

// [R7-BREAK] Store-B write used by the breakables lane: a destructible WALL
// (BP_BreakableWall_C) keeps its broken-ness in store B under the FName its own
// `WallIsBrokenName` property names, so the peer that applies a wall break has to
// write that record too -- otherwise the wall comes back after the next reload.
// Writes through SpartaWorldStateHelper::WriteAnInt (plain set), reads it back and
// returns whether the value landed.
bool StoreBWriteInt(const std::string& owner, const std::string& valueName, int value);

} // namespace progress

// ===========================================================================
// 14b. [R7-BREAK] live breakables (native\src\coop_world_break.cpp)
//
// Crates/pots/barrels/carts/barriers are `BP_VFX_Breakable_C` actors with NO
// save component (data mine section 19), so no store can remember them: only a
// live event can.  The wire key is
//     ,"wx":[["<8 hex>","B",<seq>], ...]
// <8 hex> = world::Hash(the placed actor name), the same FNV-1a every other world
// key uses; <seq> is this sender's monotone event counter, so a repeated, lost or
// reordered packet is harmless (the receiver keeps the highest seq per key).
// BRK_OFF is BREAK_OFF.txt (Toggles::breakEnabled): inert module when present.
// ===========================================================================
namespace brk {

void Tick2();                                // 1 Hz scan + 2 Hz apply + payload
void Reset();                                // a new world: drop the actor pointers
void OnPeerLine(const std::string& line);    // the ",wx":[...] section
void Serialize(std::string* out);            // appends ,"wx":[...] (nothing when idle/off)

std::string Status();                        // the "brk=" sync.txt line

// Test hooks (cmd.txt)
void BreakDump(const std::string& arg);      // breakdump [all] -- name/key/location of every breakable
void BreakNow(const std::string& arg);       // brkbreak [nearest|<key>] -- break one here
// [R7-BREAK2] the probe pair the game's own break entry was found with:
void BreakState(const std::string& arg);     // brkstate [all|nearest|<key>] -- live state + which UFns exist
void BreakCall(const std::string& arg);      // brkcall <Fn> [nearest|<key>] -- invoke ONE named UFunction

} // namespace brk

// ===========================================================================
// 14c. [R8-WALL] going THROUGH the broken wall (native\src\coop_wall_anim.cpp)
//
// The destructible wall (`BP_BreakableWall_C`) opens a squeeze traversal; the way
// through is the game's own montage
//   .../Traversal/SqueezeThrough/A_Shared_Traversal_SqueezeThrough_Montage
// played by GA_Traversal_SqueezeThrough_C from
// BP_Interaction_Traversal_SqueezeThrough_C.  The actor moves through the gap for
// the montage's duration, so the puppet must hold that pose AND follow the gap
// instead of falling back to locomotion / snapping.
//
// Wire: ,"wa":{...} -- ONE object, sent when a traversal starts and repeated while
// one runs (4 Hz), with a per-sender seq so a lost or doubled packet is harmless:
//   "s":<seq>,"p":"<segment asset tail>","r":<rate>,"d":<duration>,
//   "a":[x,y,z],"b":[x,y,z]        (gap entrance / exit of THIS client, rounded)
// WALLANIM_OFF.txt (Toggles::wallAnim) makes the module inert.
// ===========================================================================
// [R8-WALL] begin
namespace wall {

void Tick2();                                // 2 Hz: notice a traversal, publish it
void Tick30(double dt);                      // 30 Hz: follow the gap (the puppet side)
void Reset();                                // world change: drop the state
void OnPeerLine(const std::string& line);    // the "wa" field of the peer line
void Serialize(std::string* out);            // appends ,"wa":{...} (nothing when idle/off)

std::string Status();                        // the "wall=" line of sync.txt

// True while the PEER is in a traversal: coop_puppet's UpdateAnimation must hold
// the traversal pose instead of falling back to locomotion when the segment asset
// is not loaded here (the "it slides through the wall" artefact), and its drive
// must not take the 400 uu snap in the middle of the gap.
bool PeerTraversal();
// The peer's traversal (the segment asset tail it published), for the pose hold.
const std::string& PeerAsset();
int PeerSeq();                               // the peer's traversal counter
// True while OUR client is in a traversal (published as "wa").
bool OwnTraversal();

// Lab verbs (cmd.txt)
void Cmd(const std::string& cmd, const std::string& rest); // wallscan, wallplay, wallprobe

} // namespace wall
// [R8-WALL] end

// ===========================================================================
// 15. [R5-BODY] zone identity + the friend's body state (native\src\coop_body.cpp)
//
// Two features, one file, because both answer "what is the peer's world/body
// doing right now" from the live engine rather than from a save object.
//
// zone: the published identity is "zn" = "<persistentMap>|<zoneId>|<liHash>".
//   persistentMap is the world's package path, zoneId the active Sparta zone,
//   liHash a hash of the loaded level-instance package names.  When both sides
//   know their map and the maps differ, Paused() is true and coop_net's apply
//   dispatch skips EVERY peer world-state apply (store A/B, ledger, quests, data
//   layers, lifts, chests, enemy mirroring/ownership/spawn replay/aitarget, peer
//   damage) and the peer's body is not drawn -- every one of those keys is a
//   per-map actor name, so applying them across maps can only write nonsense.
//   Publishing continues, so the guard clears itself when the maps agree again.
//
// body: "bh":[hp,hpMax,shp,shpMax,res,resMax,heal,healMax,pp,ppMax,
//             ammo,ammoMax,dark,ds] at 2 Hz.  The receiver writes the values into
//   the PUPPET's own attribute sets (so the game's UI/AI see the friend's real
//   numbers) and plays a death clip on the puppet while the peer's ds >= 1.
// ===========================================================================
// [R5-BODY] begin
namespace zone {

void Tick2();                              // 2 Hz: recompute the local "zn", run the guard
void OnPeerLine(const std::string& line);  // the "zn" field of the peer line
void Reset();                              // world change: drop every engine pointer

void SerializeZn(std::string* out);        // ,"zn":"<map>|<zone>|<liHash>"   ("" = omit)

bool SameMap();                            // both sides know their map and they agree
bool PeerKnown();                          // a "zn" arrived at least once this session
const std::string& Local();                // the local identity string we publish
const std::string& Peer();                 // the peer's last "zn"
bool Paused();                             // THE guard: peer world state must not be applied

// Counters for the ZONE verdict ("the apply counters stop moving"): called by
// net's apply dispatch once per peer line, with what the guard decided.
void NoteApply(bool paused);

std::string Status();                      // the "zone=" line of sync.txt
void CmdFake(const std::string& rest);     // zonefake <map>|off

} // namespace zone

namespace body {

struct State {
    double hp = 0.0, hpMax = 0.0;      // USpartaHealthSet.Health / MaxHealth
    double shp = 0.0, shpMax = 0.0;    // ShellHealth / MaxShellHealth
    double res = 0.0, resMax = 0.0;    // Resolve / MaxResolve
    double heal = 0.0, healMax = 0.0;  // PlayerAttributeSet HealCurrent/MaxCharges
    double pp = 0.0, ppMax = 0.0;      // PlayerAttributeSet PPItemResource / Max
    double ammo = 0.0, ammoMax = 0.0;  // SidearmAttributeSet CurrentAmmo / MaxAmmo
    int dark = 0;                      // BPSO_PlayerSession_C.IsInDarkform
    int ds = 0;                        // USpartaHealthComponent.DeathState 0/1/2
    bool known = false;                // false until a complete sample succeeded
    double at = 0.0;                   // net::Now() of the sample (peer side: of receipt)
};

void Tick2();                              // 2 Hz: sample our body, write the peer's into the puppet
void OnPeerLine(const std::string& line);  // the "bh" field of the peer line
void Reset();

void SerializeBh(std::string* out);        // ,"bh":[...]  (omit while unknown or BODY_OFF)

const State& Local();
const State& Peer();
double PeerAge();                          // seconds since the peer's last "bh"

// True when the puppet's current normalized health is a value THIS module wrote
// from the peer's state.  combat::PuppetDamage asks before it treats a drop as
// damage our world dealt: without it every peer heartbeat came back as a hit.
bool OwnsPuppetHealth(double hpNorm);

// True while the peer is dead (ds >= 1) and the death clip owns the puppet mesh;
// coop_puppet's UpdateAnimation must not play locomotion over it.
bool PuppetDead();

std::string Status();                      // the "bh=" line of sync.txt
void CmdSet(const std::string& rest);      // bodyset <field> <value>
void CmdDump();                            // bodydump

} // namespace body
// [R5-BODY] end

// ===========================================================================
// 16b. coop_farenemy -- the host's far-away replayed enemy (R8)
// ===========================================================================
// A replayed enemy on the client whose player is NOT near it used to be destroyed
// by the game ~0.4-1.6 s after it spawned, at full health (docs/R6-SPAWN-REPORT.md
// section 6, open item).  This module watches the claimed spawners' enemies, records
// how they die (EndPlayReason + engine life span) and keeps the copy the peer is
// responsible for alive (REPORT.md).  Owner: coop_farenemy.cpp.
namespace farenemy {

void Tick15();                              // 15 Hz: watch + the life-span fix
void Reset();                               // a world change frees the actor pointers

void CmdDump(const std::string& rest);      // fardump: claimed enemies, life span, cell state, sources
void CmdFix(const std::string& rest);       // farfix <off|life|stream|all|auto>
void CmdUFunc(const std::string& rest);     // farufunc <loRva> [<hiRva>]: which UFunction owns that code
void CmdHook(const std::string& rest);      // farhook [off|<rva>]: install/report the destroy veto

} // namespace farenemy

// ===========================================================================
// 16c. [R10] Part A + Part B modules (HitHook, Puppet+, Steam P2P, Menu,
//            Beacons/Cutscenes/Scaling, Projectiles/Grabs/VFX, Boss/Dungeon/Puzzles/Abilities)
// ===========================================================================
namespace hithook {
void Tick2();
void Reset();
void Serialize(std::string* out);
void OnPeerLine(const std::string& line);
std::string Status();
bool OnCommand(const std::string& cmd, const std::string& rest);
} // namespace hithook

namespace puppet_plus {
void Tick2();
void Reset();
std::string Status();
} // namespace puppet_plus

namespace steam {
constexpr int kStateIdle      = 0;
constexpr int kStateStarting  = 1;
constexpr int kStateWaiting   = 2;
constexpr int kStateConnected = 3;
constexpr int kStateError     = 4;

struct Config {
    int         mode          = 0;           // 0 = host, 1 = join
    int         network       = 0;           // 0 = steam, 1 = lan
    int         steamIdentity = 0;           // 0 = game, 1 = spacewar (480)
    uint16_t    port          = 27050;
    std::string password;
    std::string server        = "127.0.0.1";
};

void LoadConfig();
void SaveConfig(const Config& cfg);
Config GetConfig();
uint32_t ActiveSteamAppId();
bool RestartWithAppId(uint32_t appId, std::string* err);
bool StartSession(bool isHost, std::string* err);
void StopSession();
bool IsActive();
void SendPacket(const std::string& text);
bool PopPeerPacket(std::string* out);
int State();
int Role();
std::string StatusText();
std::string PeerName();
std::string Status();
} // namespace steam

namespace menu {
void Tick30();
void Tick2();
void Reset();
bool IsOpen();
} // namespace menu

namespace beacon_scale {
void Tick2();
void Reset();
void Serialize(std::string* out);
void OnPeerLine(const std::string& line);
std::string Status();
} // namespace beacon_scale

namespace fx_grabs {
void Tick15();
void Tick2();
void Reset();
void Serialize(std::string* out);
void OnPeerLine(const std::string& line);
std::string Status();
} // namespace fx_grabs

namespace partb {
void Tick2();
void Reset();
void Serialize(std::string* out);
void OnPeerLine(const std::string& line);
std::string Status();
} // namespace partb

// ===========================================================================
// 17. inline implementations of the containers
// ===========================================================================

inline int* StrIntMap::Find(const std::string& key) {
    for (auto& it : items_) {
        if (it.key == key) return &it.value;
    }
    return nullptr;
}

inline const int* StrIntMap::Find(const std::string& key) const {
    for (const auto& it : items_) {
        if (it.key == key) return &it.value;
    }
    return nullptr;
}

inline int StrIntMap::Get(const std::string& key, int def) const {
    const int* p = Find(key);
    return p ? *p : def;
}

inline void StrIntMap::Set(const std::string& key, int v) {
    if (int* p = Find(key)) {
        *p = v;
        return;
    }
    StrInt row;
    row.key = key;
    row.value = v;
    items_.push_back(row);
}

inline bool StrIntMap::Erase(const std::string& key) {
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].key == key) {
            items_[i] = items_.back();
            items_.pop_back();
            return true;
        }
    }
    return false;
}

inline double* StrNumMap::Find(const std::string& key) {
    for (auto& it : items_) {
        if (it.key == key) return &it.value;
    }
    return nullptr;
}

inline const double* StrNumMap::Find(const std::string& key) const {
    for (const auto& it : items_) {
        if (it.key == key) return &it.value;
    }
    return nullptr;
}

inline double StrNumMap::Get(const std::string& key, double def) const {
    const double* p = Find(key);
    return p ? *p : def;
}

inline void StrNumMap::Set(const std::string& key, double v) {
    if (double* p = Find(key)) {
        *p = v;
        return;
    }
    StrNum row;
    row.key = key;
    row.value = v;
    items_.push_back(row);
}

inline bool StrNumMap::Erase(const std::string& key) {
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].key == key) {
            items_[i] = items_.back();
            items_.pop_back();
            return true;
        }
    }
    return false;
}

inline bool StrSet::Has(const std::string& key) const {
    for (const auto& it : items_) {
        if (it == key) return true;
    }
    return false;
}

inline bool StrSet::Add(const std::string& key) {
    if (Has(key)) return false;
    items_.push_back(key);
    return true;
}

inline bool StrSet::Erase(const std::string& key) {
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (items_[i] == key) {
            items_[i] = items_.back();
            items_.pop_back();
            return true;
        }
    }
    return false;
}

inline std::string* StrStrMap::Find(const std::string& key) {
    for (auto& it : items_) {
        if (it.key == key) return &it.value;
    }
    return nullptr;
}

inline const std::string* StrStrMap::Find(const std::string& key) const {
    for (const auto& it : items_) {
        if (it.key == key) return &it.value;
    }
    return nullptr;
}

inline std::string StrStrMap::Get(const std::string& key, const char* def) const {
    const std::string* p = Find(key);
    return p ? *p : std::string(def ? def : "");
}

inline void StrStrMap::Set(const std::string& key, const std::string& v) {
    if (std::string* p = Find(key)) {
        *p = v;
        return;
    }
    StrStr row;
    row.key = key;
    row.value = v;
    items_.push_back(row);
}

inline bool StrStrMap::Erase(const std::string& key) {
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].key == key) {
            items_[i] = items_.back();
            items_.pop_back();
            return true;
        }
    }
    return false;
}

} // namespace coop
