# Round 7 — `r7-maps`: store-B float/name maps (G2) + live breakables (G3)

Workspace `G:\Freespace\shell_coop\wt\r7-maps` · installs host `E:\Games\_MS2-coop-i`,
guest `E:\Games\_MS2-coop-j` · relay port 47140 · build `native\build.bat test`
(`coop_policy: 112 checks passed, 0 failed`, `Built:`).

Verdict lines this round added: **`BREAK`** in `run-two.ps1` (log-based, written by the
receiving client) and the visual pair in `break-lab.ps1`.

| Item | Result | Evidence |
|---|---|---|
| G2 `ActorFloats` (0x78) | **EMPTY — never written** (0 slots, before *and* after a full session) | `mapsdump` lines below |
| G2 `ActorNames` (0x118) | **2 entries, only `Sparta_Player`-owned** (per-player respawn record), unchanged in a session → **no sync built, by design** | `mapsdump` lines below |
| G3 live breakables | **implemented + verified on the wire and on the peer's objects** — `BREAK: PASS`; **on-screen before/after of the specific prop: WARN** (§4.3) | `run-two` transcript, `result_break.txt`, screenshots |

---

## 1. Part 1 — does the game use the store-B float and name maps at all?

### 1.1 The probe (`mapsdump`)

`coop_progress.cpp` grew one read-only verb, `mapsdump [all|ints|floats|names|bools]`.
It walks **all four** store-B maps with the same raw TMap walker `wbScan` uses
(`WalkMapProp` → `ue::MapForEachRaw`; the property-based `ue::MapForEach` is broken on
this build's `FMapProperty` — see the comment above `WalkMapProp`), and prints one line
per map plus one line per float/name entry (`owner|valueName = value`).

One addition was needed to keep the answer honest: `MapForEachRaw` cannot tell an
**empty** map from an **unreadable** one (`ReadMapInfo` requires a non-zero allocation-bit
count), so `ms2_ue.cpp` gained `ue::MapCountRaw(map, &num, &max)` — the sparse array's own
slot count read straight from the header. `slots=0 EMPTY` is the definitive "the game
stores nothing here"; `slots>0` with `entries=0` would have been a walker bug instead.

### 1.2 The count — same numbers before and after a whole play session

`run-two.ps1` on i/j (port 47140) issues `mapsdump` twice: once right after `phase=sync`
and once after a door was opened, a currency bag picked up, an enemy killed, a chest
looted and the hub lift used (the WORLD / PROGRESS / QUEST / CHEST / LIFT blocks). A
single-client lab run (`lab.ps1` on i) and the second full run reproduced the same answer;
the exact `ActorInts` growth varies with what the session touched (2 → 9 slots in the
session quoted below, 2 → 8 in the final one) — `ActorFloats` stayed at `slots=0` in
**all** of them.

```
[01:03:30][A] mapsdump: ActorInts   path=raw  entered=1 off=0xC8  slots=2 cap=2  owners=2 inner=2  kept=2  matchable=2
[01:03:30][A] mapsdump: ActorBools  path=raw  entered=1 off=0x168 slots=3 cap=3  owners=3 inner=20 kept=20 matchable=20
[01:03:30][A] mapsdump: ActorFloats path=NONE entered=0 off=0x78  slots=0 cap=0  owners=0 inner=0  kept=0  matchable=0 EMPTY
[01:03:30][A] mapsdump ActorNames Sparta_Player|ProloguePlayerStart = PrologueStart_HUB_Bridge
[01:03:30][A] mapsdump ActorNames Sparta_Player|RespawnPlayerStart  = LandingArea_HUB
[01:03:30][A] mapsdump: ActorNames  path=raw  entered=1 off=0x118 slots=1 cap=1  owners=1 inner=2  kept=2  matchable=2

… door / pickup / kill / chest / lift …

[01:05:06][A] mapsdump: ActorInts   path=raw  entered=1 off=0xC8  slots=9 cap=21 owners=9 inner=9  kept=9  matchable=9
[01:05:06][A] mapsdump: ActorBools  path=raw  entered=1 off=0x168 slots=3 cap=3  owners=3 inner=20 kept=20 matchable=20
[01:05:06][A] mapsdump: ActorFloats path=NONE entered=0 off=0x78  slots=0 cap=0  owners=0 inner=0  kept=0  matchable=0 EMPTY
[01:05:06][A] mapsdump ActorNames Sparta_Player|ProloguePlayerStart = PrologueStart_HUB_Bridge
[01:05:06][A] mapsdump ActorNames Sparta_Player|RespawnPlayerStart  = LandingArea_HUB
[01:05:06][A] mapsdump: ActorNames  path=raw  entered=1 off=0x118 slots=1 cap=1  owners=1 inner=2  kept=2  matchable=2
```

Reading of that table:

* **`ActorFloats` is empty and stays empty.** `slots=0` in the header, on both clients
  (`brk`/`wb` counters agree) and at every sample. Nothing in the shipped content of the
  test save writes a float through `WriteAnFloat`/`WriteMyFloat`. **No float sync was
  built** — the wire slot would have carried nothing.
* **`ActorNames` is used, but only by `Sparta_Player`.** Two entries, both under the
  local player's own record: `ProloguePlayerStart` and `RespawnPlayerStart` (where this
  player's prologue/respawn spawn is). They did **not** change during the session, and
  they are the *player's own* checkpoint bookkeeping.
* The `ActorInts` row is the control that proves the walker works: the game itself added
  7 ints during that session (2 → 9 slots), which is exactly what the `wb` lane exists for.

### 1.3 Decision: no sync for either map, and why (rather than "the map is empty")

`ActorNames` is *not* empty, so "nothing to carry" would be the wrong summary. The honest
reason it is not synced is:

1. **The existing pipeline already drops these rows by design.** `wbScan` skips the owner
   `Sparta_Player` (`if (owner == "Sparta_Player") return true;` in `ScanSaveMap`) because
   that owner is "the local player's own record (tutorials, counters)". A `wn` section
   built by cloning `wbScan` would therefore publish exactly **zero** entries — measured,
   not assumed: `matchable=2` but `owner == Sparta_Player`.
2. **Syncing them would be wrong even if it published something.** `RespawnPlayerStart`
   is *where this player respawns*. Copying player A's value onto player B would teleport
   B to A's checkpoint — the same class of mistake as syncing inventory, gold or levels,
   which the project rules forbid.

So: **report, do not build**, and this is the probe that would change the answer — if a
future save ever contains an `ActorNames` entry whose owner is a *level actor* (e.g.
`BP_SomeActor_C_12|SomeName`) or a non-zero `ActorFloats` slot count, the same verb prints
it and a `wf`/`wn` section mirroring the `wb` mechanics becomes justified. No
`MAPS_OFF.txt` was created because no map sync exists to switch off.

---

## 2. Part 2 — live breakables (crates, pots, barrels, carts, barriers, destructible walls)

### 2.1 What the game has (evidence: `research\raw\CXXHeaderDump\BP_VFX_Breakable.hpp`,
`BP_BreakableWall.hpp`, `research\10-world-state.md` §8.6, data mine §19)

| Class | Live state | Persisted? |
|---|---|---|
| `BP_VFX_Breakable_C` (`: BP_VFX_Physical_C`; parent of **92** `BP_VFX_Breakable_*` props: barrels, jars, carts, tables, chandeliers, battleground barriers …) | `IsBroken: bool @0x518`, `Thresholds`, `KillAfterDestruction @0x570`, `OnBroken` delegate `@0x578`; functions `TriggerDamage`, `CustomBreak`, `BreakableHit`, `FilteredHit`, `OnBrokenEvent`, `ConvertToBreakable`, `SpawnDisable`, `Cleanup` | **no** — not a single `USpartaSaveComponent` in its 40+ members |
| `BP_DestructiblePlaceholder*_C` (wall, wall_OLD, cart, chandelier, fruitbasket, ground placeholder, ThornBoiRitual …) | inherited `IsBroken` | **no** |
| `BP_BreakableWall_C` | `WallIsBrokenName: FName @0x2E0`, `WallIsBroken: bool @0x2E8`, `Wall` mesh, `SqueezeThrough: TArray<TSoftObjectPtr<BP_Interaction_Traversal_C>>`; functions `UnlockTraversals`, `LockTraversals`, `SpartaApplyHit`, `SpartaSave` component | **yes — store B**, under `WallIsBrokenName` |
| `BP_HiddenWall_C` (`SaveWallRevealed`), `BP_PrisonCeilingBreak_C` | store A (`BP_SO_SimpleBool_C`) | yes |

So the gap is exactly as G3 describes: a barrel's broken-ness exists **only** as a live
bool on the actor, so **no store can replay it** — it has to be an event. The wall is the
one breakable that persists *and* opens a path (`SqueezeThrough`), which is the
"must not re-block on the peer" case in the task.

### 2.2 How the game breaks one (and what a receiver may safely do)

The chain in the class is `SpartaApplyHit` → `TriggerDamage(Instigator, Location, Velocity,
Radius, AngularVelocity, StrainRadius)` → `FilteredHit(...)` → `BreakableHit(Location,
FirstHit)` → Chaos field → `OnChaosBreakEvent` → `IsBroken = true` + `OnBroken` delegate.
`CustomBreak(FHitResult, Instigator)` is the same thing entered with a hit result.

* **Detection** therefore needs nothing but `IsBroken` — no hook, no FHitResult.
* **Apply on the receiver** must not re-run a physics break chain on a guess (G3's warning
  about constructing an `FHitResult` is right: every pointer in that 0x100 struct would be
  one the graph may dereference). So the receiver uses the two primitives this project has
  already proven for "this object is gone" (`coop_world.cpp` chest path): write the actor's
  own state bool, then `SetActorHiddenInGame(true)` + `SetActorEnableCollision(false)`.
  The mandate is "the same object breaks on the peer"; the player sees it leave the world
  and cannot walk into it, and the game's own script reads `IsBroken == true`.
* **Persistent wall**: additionally `UnlockTraversals()` **and** a store-B write on the
  peer (`progress::StoreBWriteInt(actorName, WallIsBrokenName, 1)`, the value name read off
  the actor itself), so the path stays open after a reload instead of re-blocking. This is
  the one place where a live event has to touch store B.

### 2.3 The stable key: the **placed actor name**, not a location

`world::Hash(<actor name>)` — the same FNV-1a `ws`/`wt` use — where the actor name is the
`PersistentLevel.<name>` token of `GetFullName()`. Both clients load the same `.umap` in the
same order, so the persistent level names its actors identically on both sides.

A location hash was rejected on purpose: every one of these props is a *simulated physics
body* (`BP_VFX_Physical_C`), and the two clients simulate them independently — after one
nudge, one hit or one Chaos impulse the two copies are metres apart, so a location key
would stop matching exactly when it is needed. `breakdump` prints **both** so the claim is
checkable: the names are identical across clients, the locations are not stable.

Proven in the run below: A published `key=75e02c2e` for
`BP_VFX_Breakable_Cart_02_A_C_UAID_345A60361B8776BE02_83a1dc45e2a01438_2105289101_2105289102`
and B resolved the **same** key to the **same** class and the **same** name.

### 2.4 The wire (`wx`) — own top-level key, small, seq-based

```
,"wx":[["<8 hex>","B",<seq>], ...]
```

* `<8 hex>` — `world::Hash(placed actor name)`; `"B"` — the event kind (break), room for more.
* `<seq>` — this sender's monotone per-event counter. The receiver keeps the highest seq
  applied per key, so a **lost, repeated or reordered packet is harmless** (run: B received
  the same event 4 times and applied it once).
* An event travels for 25 s (`kEventKeep`, ≤40 entries per packet, ring of 64) so a dropped
  packet costs a delay, never a state. The section is omitted entirely when there is
  nothing to send (no bytes on the wire when idle).
* Existing keys are untouched; the section is appended **after** `wc`/`brk`… i.e. last,
  outside the `wsEnabled` block (a barrel is not world *save* state, and `BREAK_OFF.txt`
  is its own switch, so `WS_OFF.txt` does not have to remove it). The receiver parses it in
  `ApplyPeer` **inside the zone guard** (`zone::Paused()`), because every key is a per-map
  actor name — the same rule the rest of the world dispatch obeys.

### 2.5 Echo guard (the "false-free" requirement)

The apply writes `IsBroken=true` on **our** copy, which is exactly the transition our own
1 Hz scan watches. A key this client applied from the peer is latched (`peerApplied`) and
its flip is logged as
`break echo <key> … (this client applied it on the peer's order -- not published)` and is
never published. Measured in the run: A `sent:1`, B `sent:0` while B `applied:1` — no
ping-pong. A latch, not a window: a broken barrel never un-breaks, so a one-shot latch is
the correct shape here (`gSelfWriteSeq`/`gFalseSeq`-style windows are for keys that cycle).

### 2.6 Kill switch

`BREAK_OFF.txt` in the install's `coop\` dir → `Toggles::breakEnabled = false`
(`Toggles::Load`, read once at load, same mechanism as `BODY_OFF.txt`/`WS2_OFF.txt`):
no scan, no `wx` written or read, no apply. Template with instructions:
`<ws>\BREAK_OFF.txt`. Verified: §4.3.

---

## 3. Diff summary

New files:

| File | What |
|---|---|
| `native\src\coop_world_break.cpp` | the whole lane (~700 lines): `ScanOnce`, `PublishFlip`, `ApplyBreak`, `HideActor`, `StateField`, `PeerNote`, `ReadPeer`, `ApplyPending`, `BuildPayload`, `FinishLocalTry`, `Tick2`, `Reset`, `OnPeerLine`, `Serialize`, `Status`, `BreakDump`, `BreakNow` |
| `ms2-coop\break-lab.ps1` | two-client visual run: both players teleported into the barrel cluster, A-side BEFORE/AFTER shots and B-side BEFORE/AFTER shots, `breakdump` on both, logs copied to `local\lab\<stamp>-r7maps-break\` |
| `BREAK_OFF.txt` | kill-switch template + how to use it |

Modified (every change inside `[R7-…] begin/end` markers):

| File | Function / place | Change |
|---|---|---|
| `native\src\coop_progress.cpp` | `PaddedValueSize`, `DumpSaveMap`, `MapsDumpBody`, `MapsDump` | new: the G2 count (`mapsdump`) |
| | `StoreBWriteInt` | new public: one store-B int write with readback (used by the wall apply) |
| `native\src\ms2_ue.cpp` / `ms2_ue.h` | `MapCountRaw` | new: the TMap header's slot count, to tell "empty" from "unreadable" |
| `native\src\coop_net.cpp` | `Toggles::Load` | `BREAK_OFF.txt` row |
| | `ResetAll` | `brk::Reset()` |
| | `Publish` | `brk::Serialize(&line)` (own key, after the world block) |
| | `ApplyPeer` | `Step("brkReadPeer", …)` inside the zone guard |
| | `Loop2` | `RunGuardStep("breakables", …)` → `brk::Tick2()` |
| | `WriteSyncStatus` | `brk::Status()` line (`brk=rows:… sent:… recv:… applied:… fail:… pending:… wall:…`) |
| | `RunCommands` | verbs `mapsdump`, `breakdump`, `brkbreak` |
| `native\coop_state.h` | `Toggles` | `breakEnabled` (BREAK_OFF.txt) |
| | `namespace progress` | `MapsDump`, `StoreBWriteInt` |
| | `namespace brk` | new: the lane's public surface + the `wx` format documented |
| `ms2-coop\run-two.ps1` | after the LEDGER block | the `BREAK` verdict (written by the receiving client) |
| | boot block / after CHEST | `mapsdump` ×2 (the G2 session diff) |
| | `$pass` | `-and $breakOk` |

Cost: the scan is **1 Hz** (one `FindAllOf` per second) and the rest of the lane is two
`net::Step`s per 2 Hz tick. `sync.txt` shows no new slow step; the 2 Hz loop stayed inside
its usual band on both clients in the run below (`PROF slow t2 loop` maxima come from
`FindObject` for animations, not from this lane).

---

## 4. Tests run, and their verdict lines

### 4.1 Build

```
G:\Freespace\shell_coop\wt\r7-maps\ms2-coop\native\build.bat test
  coop_policy: 112 checks passed, 0 failed
  Built: …\native\build\dxgi.dll            (exit 0)
```

### 4.2 `run-two.ps1` (i/j, port 47140) — `RESULT: PASS` on the SHIPPED build

Transcript of the final run (`dxgi.dll` sha256
`e7ea9db82c05700861bed7eacdda2bdbb9c5d1590dfd99d366958b69ee6d1b66`):
`G:\Freespace\shell_coop\local\lab\run-two-r7maps-final.txt`. It was run **twice** — the
first PASS was on the build before `brkbreak` learned to re-resolve a re-created actor
(§4.3's teleport pitfall), so the whole suite was re-run on the shipped DLL and is green
again; both runs printed `RESULT: PASS` and `BREAK: PASS`.

Verdict lines (the differing counters are each run's own measured values; the final
run's exact text is in the transcript):

```
=== SHELL: PASS
=== WEAPON: PASS
=== PUPVIS: PASS (A PASS puppet render: visible=1 animmode=1 animinstance=AnimSingleNodeInstance weapons=3/3 flat=0 ; B same)
=== LOOK: PASS (on=True off=True)
=== ANIM: PASS puppet locomotion: sequence states idle=566 walk=20 sprint=0, moving-while-animated=20 ticks
=== DAMAGE: PASS
=== DEATH: PASS
=== WORLD: PASS world state: door BP_HubDoubleDoor_C_… opened, BP_CurrencyBag_Pickup_C_… collected (live=2 base=0 fail=0)
=== PROGRESS: PASS world progression: i|BP_Interactable_Chest_General_C_…|TimesUsed, tag t|Prologue.FlowerCrownRecieved removed+restored, layer DL_AU loaded+reactivated
=== QUEST: PASS quest LocationQuest added then completed
=== QUEST2: PASS quest state GorfQuest: count heard 1 objective(s), stage s=3, tracked, rel r|Gorf=2, turned in
=== LIFT: PASS lift BP_HubLift_C_… followed the peer to floor 1
=== CHEST: PASS chest … lid replayed (anim pos 5.07), loot ID_Charges_Default… 1 -> 2
=== STORE_A2: PASS store A2: BPSO_NPC_C.HasTriggeredInitialGreeting replayed …, ClensedCores=1 applied from wt
=== STORE_A2SELFTEST: ws2 self test: 20 pass, 0 fail
=== SPAWN: PASS enemy spawned by the peer appeared here too
=== MIRROR: PASS host copy driven by the guest
=== PEERHIT: PASS peer-side hit applied to own body: 0.480 -> 0.380
=== AITARGET: PASS enemy … targets the peer's body after SetTargetOverride
=== STREAM: PASS remote area streamed around peer 91460 uu away
=== NPCFAR: PASS host hands BP_Cultist_HUB_C_… to the far guest and moves its copy there
=== LEDGER: A ok keys:12 good:0 bad:0 applied:0 ; B ok keys:12 …
=== BREAK: A rows:82 sent:1 recv:0 applied:0 fail:0 pending:0 wall:0
=== BREAK: B rows:131 sent:0 recv:6 applied:1 fail:0 pending:0 wall:0
=== BREAK: PASS peer break 75e02c2e BP_VFX_Breakable_Cart_02_A_C BP_VFX_Breakable_Cart_02_A_C_UAID_345A60361B8776BE02_83a1dc45e2a01438_2105289101_2105289102 (hidden+state)
=== BREAK: PASS
=== ZONE: PASS zone guard: … world apply paused 5.5s (82 applies skipped), resumed
=== PEERHP: PASS peer body: hp=28.500/75.000 (0.380) … ; puppet health set reads back 0.380
=== PEERDEAD: PASS peer death: ds=1 ; the puppet plays /Game/MS1/Characters/DeathsLibrary/death_093_front…
=== A: PASS puppet tracks peer (dist 0.4, 30/30 good; moves 13, snaps 0)
=== B: PASS puppet tracks peer (dist 0.4, 30/30 good; moves 13, snaps 0)
RESULT: PASS
```

The two log lines that carry the feature:

```
[01:07:01][A] break apply 75e02c2e BP_VFX_Breakable_Cart_02_A_C_UAID_…_2105289101_2105289102 (local fallback) state=set hidden=true collision=true -> broken
[01:07:01][A] break local 75e02c2e BP_VFX_Breakable_Cart_02_A_C_UAID_… (BP_VFX_Breakable_Cart_02_A_C) seq=1 -> published
[01:07:02][B] break peer 75e02c2e kind=B seq=1 (waiting for the actor)
[01:07:02][B] break apply 75e02c2e BP_VFX_Breakable_Cart_02_A_C_UAID_… (peer event) state=set hidden=true collision=true -> broken
[01:07:02][B] break verdict: PASS 75e02c2e BP_VFX_Breakable_Cart_02_A_C BP_VFX_Breakable_Cart_02_A_C_UAID_…
```

→ **~1 s** from the host's break to the guest's copy being broken, with the same key, the
same class and the same actor name on both sides, and no echo back (`B sent:0`).

### 4.3 Visual run — `break-lab.ps1` (2 clients, both in the barrel cluster)

`break-lab.ps1` (new, same shape as `visual-two.ps1`) starts i/j, waits for
`phase=sync`, teleports **both** players into the barrel cluster `breakdump` reported,
takes screenshots of both windows, breaks the host's nearest barrel, waits, screenshots
again, and dumps `breakdump` on **both** clients.

Run 2 — `local\lab\20261026-011250-r7maps-break2\`
(contact sheet `contact-break.png`, crop `compare-A.png`):

```
tp check A selfpos=45225.9 -131229.8 -41900.0 (target 45205 -131294 -41900)
tp check B selfpos=45372.2 -131984.4 -41822.3 (target 45396 -131978 -41900)
[01:14:39][A] breakdump …Wooden_Barrel_a_Closed_01_C… key=b76e4067 d=91  at=(45205 -131294 -41960) intact
[01:14:46][A] break local b76e4067 …Wooden_Barrel_a_Closed_01_C… seq=1 -> published
[01:14:47][B] break peer b76e4067 kind=B seq=1 (waiting for the actor)
[01:14:47][B] break apply b76e4067 … (peer event) state=set hidden=true collision=true -> broken
[01:14:47][B] break verdict: PASS b76e4067 BP_VFX_Breakable_Wooden_Barrel_a_Closed_01_C …
[01:14:54][B] breakdump … key=b76e4067 d=724 at=(45205 -131294 -41960) BROKEN (peer-applied)
[01:14:54][B] breakdump … key=8f4d91cb d=838 … intact
[01:14:54][B] breakdump … key=3ce76897 d=767 … intact
=== brk A rows:5 sent:1 recv:0 applied:0
=== brk B rows:5 sent:0 recv:3 applied:1
```

So on the **peer**, at the end of the run, exactly **one** of the five tracked breakables
reads `BROKEN (peer-applied)` and it is the **same key and the same actor name** the host
broke; its three neighbours still read `intact`. `B recv:3 applied:1` = the event arrived
three times (the 25 s repeat window) and was applied once — the seq dedupe works.

Run 3 — `local\lab\20261026-011638-r7maps-break3\` (contact sheets `contact-before.png`,
`contact-after.png`, `B-views.png`), shot as a 5-frame burst while the pawn walks (patrol
swings the camera; the mod cannot set the yaw):

```
tp check A selfpos=45205.0 -130560.0 -41961.5 (target 45205 -130560 -41900)   … landed, no fall
tp check B selfpos=45396.0 -131050.0 -41910.7 (target 45396 -131050 -41900)   … landed, no fall
[01:18:33][A] brkbreak: BP_VFX_Breakable_Wooden_Barrel_a_Closed_01_C…_2105294103_2105294104 d=765 key=8f4d91cb
[01:18:35][A] break local 8f4d91cb …Wooden_Barrel_a_Closed_01_C… seq=1 -> published
[01:18:36][B] break verdict: PASS 8f4d91cb BP_VFX_Breakable_Wooden_Barrel_a_Closed_01_C … (hidden+state)
=== brk A rows:5 sent:1 … ; B rows:5 sent:0 recv:1 applied:1
alive=2
```

**What the screenshots do and do not show — reported as WARN, not as PASS:**

* **Shown:** `contact-before.png` frame 2 has a **wooden barrel standing next to the
  player** in the cluster (the family and the place the event is about); both windows
  render the cave normally in the run-2 pair (`AB-views.png`), both clients stayed on the
  floor at the teleport target in run 3, and **neither client crashed** (`alive=2`).
* **Not shown:** a pixel-level *before → after* of the **specific** barrel disappearing.
  The `patrol` camera swing in the after-burst went **inside the cave geometry** (frames
  1/2/3/4/5 of `contact-after.png` are black with the HUD on top) and the peer's window is
  aimed at a cart, not at the barrels (`B-views.png`: three identical frames). The
  screenshots therefore confirm *where* and *that it runs*, while the object-level proof
  that the peer's copy really broke is the log: on B, **exactly one** of the five tracked
  breakables reads `BROKEN (peer-applied)` — the same key/class/name the host broke — and
  its three neighbours still read `intact`.
* **The shot that would close it** (one run, ~6 min): break the barrel the **puppet** is
  standing next to (`tp` B onto a barrel, then `brkbreak nearest` on A) and photograph
  **A's** window — the puppet is guaranteed to be in A's frame (the existing
  `visual-two.ps1` A-sees-B 8-direction trick), so the barrel in front of the puppet has a
  matched before/after with a known anchor in the picture.

A second, real teleport pitfall from these runs (it cost one run and is a property of these
props): the first attempt teleported to the props' own Z **minus 240** and the pawn **fell
through the floor** (10 000 uu down; `selfpos` stayed at the spawn, B's puppet ended at
z=-52245). These barrels stand on a floor ~170 uu **above** the spawn floor, so a teleport
into them must target slightly **above** the props' own Z. `break-lab.ps1` now prints
`selfpos` after the teleport, so a repeat of that failure is visible in the log instead of
silently wasting the run.

### 4.4 Regression hygiene

* `has no parameter` / `faulted` grepped in **both** native logs after every run:
  **none** (`TriggerDamage`'s six parameter names — including the ones with spaces,
  `Damage Instigator`, `Damage Location`, `Damage Velocity`, `Damage Angular Velocity` —
  all resolved; a wrong name would have logged `Params: … has no parameter '…'`).
* `ws_ledger_*.txt` deleted on both installs before the run (R7-COMMON).
* No game ran while editing; the run lock was taken for deploy/run and released after.
  `E:\Games\Mortal Shell II` was never touched.

---

## 5. Open items / honest limitations

1. **`TriggerDamage` is callable but does not break the actor.** In both runs the verb's
   first attempt returned `ok=true` (`brkbreak: TriggerDamage … ok=true (r=80 strain=30)`)
   and `IsBroken` still read false 2 s later, so the verb finished with the apply-path
   primitives and **said so**:
   `brkbreak: the game's break chain did NOT fire on … -- finishing with the apply-path
   primitives (honest fallback, see REPORT.md)`.
   Consequence, stated plainly: the *local* break used in the test is produced by the same
   primitives the wire apply uses, **not** by the game's own physics chain. The **detection
   path is still the real one** — the 1 Hz scan saw the false→true transition on `IsBroken`
   and published it, which is the code path a genuine in-game break (enemy hitting a barrel,
   player attack) would take. What is *not* yet proven is that a genuine break looks the
   same; the probe that settles it is next:
   * `brkbreak` chaining `CustomBreak(FHitResult=zeroed, Instigator=pawn)` before the
     fallback, and a test barrel hit by an actual attack (an `attack` verb, or standing a
     cultist next to a barrel — `hurt` on an enemy already works, so an enemy-vs-barrel
     scenario is reachable with `npctp` + `kill`).
   * Possible real reason: `TriggerDamage` only stores `Damage Instigator/Location` and
     drives a strain field, while the break itself is gated on `Threshold`/`Thresholds`
     (`0x4A0-0x4F0`), which our values (velocity 1.0, radius 80, strain 30 — the actor's own
     properties) may not exceed. Reading `Threshold` and passing a value above it is the
     cheap next experiment.
2. **The receiver hides instead of shattering.** The peer's copy disappears and loses
   collision instead of spawning the Chaos debris the breaking client sees. Deliberate
   (G3's FHitResult hazard); the same `BreakableHit`/`CustomBreak` chain could be attempted
   from the receiver once item 1 proves which entry point actually breaks.
3. **Only `PersistentLevel.<actor>` actors are keyed.** Breakables inside a level instance
   are counted (`breakdump` prints `skipped N not under a persistent level`) and skipped;
   the counter was 0 in the hub, so nothing was lost in the tested map, but a dungeon run
   would need the level-instance part of the outer name added to the key.
4. **`BP_BreakableWall_C` was never exercised live** — there is no destructible wall in the
   hub (wall count 0 in `brk=… wall:0` on both clients). The path is written (state bool,
   `UnlockTraversals`, store-B `WallIsBrokenName` write) but **unproven on screen**; the
   probe is a `brkbreak <wall key>` run in a map that has one
   (`/Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_BreakableWall`), then a reload to
   confirm it does not re-block.
5. **The float/name decision is save-dependent.** The test save is the owner's hub save. A
   different save (a dungeon in progress, a shop/forge state) could contain float entries;
   `mapsdump` is the one command that re-answers it in 10 s on any install.
6. **The visual before/after is a WARN** (§4.3): three screenshot runs put both clients in
   the barrel cluster (and one frame shows a barrel in the picture), but neither camera was
   aimed at the broken prop when the break happened, so the "gone" half is not on film yet.
   The one-run fix is written out in §4.3.

---

## 6. Files to hand to the integrator

* `native\src\coop_world_break.cpp` (**new**) — copy as-is.
* `native\src\coop_progress.cpp` — `mapsdump` (`PaddedValueSize`/`DumpSaveMap`/`MapsDumpBody`/
  `MapsDump`) and `StoreBWriteInt`.
* `native\src\ms2_ue.cpp`, `native\src\ms2_ue.h` — `MapCountRaw`.
* `native\src\coop_net.cpp` — six `[R7-…]` insertion points + `BREAK_OFF.txt` toggle row.
* `native\coop_state.h` — `Toggles::breakEnabled`, `progress::MapsDump`,
  `progress::StoreBWriteInt`, `namespace brk`.
* `ms2-coop\run-two.ps1` — `BREAK` verdict + `$pass` + the two `mapsdump` calls.
* `ms2-coop\break-lab.ps1` (**new**), `BREAK_OFF.txt` (**new**, template).

No existing wire key changed; the new key is `wx`, appended last; nothing reordered.

---

# Round 7b — visual break

The owner's follow-up: on the receiving side a breakable must **shatter like it does on the
breaker's side** (fracture pieces, VFX, debris), not just disappear. Round 7's apply was
`state bool + hidden + collision off`, which is why round 7 ended with a WARN on the
on-screen half. This round finds the game's own break entry and calls **the same one** on
the receiver.

| Item | Result |
|---|---|
| Which call really breaks a `BP_VFX_Breakable_C` | **`FilteredHit(Impulse, Location, Instigator, GenerateFieldsOnHit)`** — measured, trace below |
| Why `TriggerDamage` never flipped `IsBroken` | it only seeds the strain field; `IsBroken` is written by the BP when the **Chaos GeometryCollection actually fractures** (`OnChaosBreakEvent`) |
| Receiver path | the same `FilteredHit`, with the breaker's impact point and impulse sent in the `wx` event; old primitives only as a logged fallback |
| Visual proof | `contact-A-break.png` / `contact-B-break.png`: intact barrels → mid-air staves → debris pile, on **both** clients |
| `run-two.ps1` | `RESULT: PASS` with `BREAK: PASS` (verdict now reports the path: `(FilteredHit)`) |

## 7b.1 Root cause: `TriggerDamage` is not the break, `FilteredHit` is

`coop_world_break.cpp` grew two probe verbs for this: `brkstate` (what the actor's live state
and which of its UFunctions exist) and `brkcall <Fn>` (invoke ONE named UFunction with a
reflection-built parameter block). A single lab run on i armed the ProcessEvent trace
(`deathprobe 240 Break|Shatter|Field|Strain|Chaos|Niagara|Disable|Cleanup|…`) and called the
four candidate entries in turn, each on its own barrel:

```
[02:04:05][A] brkstate 75e02c2e BP_VFX_Breakable_Cart_02_A_C …: IsBroken=0 hidden=0 collision=1
               GC=yes fns=[CustomBreak,SpartaApplyHit,BreakableHit,FilteredHit,TriggerDamage,OnBrokenEvent,]
[02:04:08][A] brkcall CustomBreak    … called=true ok=true -> IsBroken=0
[02:04:21][A] brkcall SpartaApplyHit … called=true ok=true -> IsBroken=0
[02:04:34][A] brkcall BreakableHit   … called=true ok=true -> IsBroken=0
[02:04:46][A] brkcall FilteredHit    … called=true ok=true -> IsBroken=1      <-- the real one
[02:04:47][A] break local 75e02c2e … seq=1 -> published                       <-- the scan saw it
```

The trace in the same run says what `FilteredHit` actually did (nothing follows the other
three):

```
PROBE +  11.50s  CustomBreak                        on BP_VFX_Breakable_Cart_02_A_C …
PROBE +  37.00s  BreakableHit                       on BP_VFX_Breakable_Cart_02_A_C …
PROBE +  49.50s  BndEvt__BP_VFX_Breakable_GeometryCollection…ActorComponentActivatedSignature…
PROBE +  49.52s  BndEvt__BP_VFX_Breakable_GeometryCollection…OnChaosBreakEvent__DelegateSignature  (×N)
PROBE +  49.52s  ExecuteUbergraph_BP_VFX_Breakable  on BP_VFX_Breakable_Cart_02_A_C …
```

**The chain, measured:** `FilteredHit` → the Class's field system (Radial/Plane/Culling
fields, `FieldSystemComponent`) → the **GeometryCollection fractures** →
`OnChaosBreakEvent` fires → `ExecuteUbergraph_BP_VFX_Breakable` runs the BP's break graph
(VFX, sound, `OnBrokenEvent`, `IsBroken = true`). `TriggerDamage` (round 7's attempt) only
seeds `Damage Instigator`/`Damage Location`/the strain value; the fracture it may cause is
gated on the field/strain, which is why `r=80 strain=30` answered `ok=true` and changed
nothing. `brkstate` also shows the side effect that proves the impulse is the strength:
`thr=50.0 fR=80.0 sR=30.0` before, `fR=200.0 sR=200.0` after (the BP copies the impulse into
`Force Radius`/`Strain Radius`).

`CustomBreak`, `SpartaApplyHit` and `BreakableHit` all accept the call and do nothing on a
healthy barrel: they expect a hit that the state machine decided to break (a payload with
break damage / a first-hit test that already passed). They stay in the list, after
`FilteredHit`, because other classes (the destructible **walls**) only have
`SpartaApplyHit`.

## 7b.2 What changed (round-7b diff)

All of it in the feature's own files; nothing else touched.

| File | Change |
|---|---|
| `native\src\coop_world_break.cpp` | **apply rewritten**: an ordered `kEntries[]` of the game's own entries (**`FilteredHit` first**), `InvokeEntry` builds the parameter block **by reflection** (`ue::FindProperty` decides whether a parameter exists, `ue::PropSizeStruct` decides its width, so no float/double guess and no `has no parameter`), `HitBlob` fills a real `FHitResult` (Location/ImpactPoint/Normal/ImpactNormal/TraceStart/TraceEnd/`bBlockingHit`, layout from Dumper-7) from the **breaker's** point; a per-key `Attempt` state machine (`StartAttempt`/`PumpAttempts`/`FinishOk`) verifies by **reading the actor**; `FallbackBreak` (state+hide+collision) now runs only when every game entry failed, and is *skipped* when a game entry answered success but the state has not flipped yet (hiding would delete a real break's debris); an actor destroyed by the break counts as broken. New verbs `brkstate` / `brkcall`. Status line extended: `brk=rows:… sent:… recv:… applied:… fail:… live:… fall:… pending:… echo:…` |
| wire `wx` | one event is now `["<8hex>","B",<seq>,<x>,<y>,<z>,<imp>]` — the **impact point** (whole uu) and the **impulse** travel with the key/seq, so the receiver's `FilteredHit` happens where the breaker's did and with the same strength. Key/seq/dedupe unchanged; an old 3-field event still parses (point 0 → the actor's own location, impulse 0 → 200) |
| `native\coop_state.h` | `brk::BreakState`, `brk::BreakCall` declarations (the probe pair) |
| `native\src\coop_net.cpp` | verbs `brkstate`, `brkcall` |
| `ms2-coop\break-lab.ps1` | **rewritten for the visual proof**: reads each pawn's pose (`out_<id>.txt` x/y/z/yaw) and the nearest intact breakable from A's log (`breakdump … d=.. at=(x y z)`), teleports each pawn 450 uu from that barrel **along its own yaw** so the barrel is in front of the camera, takes BEFORE shots on both windows, breaks on A, then shoots **both windows every 300 ms for 2.4 s** (`A-break01..08` / `B-break01..08`), then `brkstate` + `breakdump` on both |

## 7b.3 Verdicts

`native\build.bat test` → `coop_policy: 112 checks passed, 0 failed`, `Built:`.

`run-two.ps1` (i/j, port 47140), full transcript
`G:\Freespace\shell_coop\local\lab\run-two-r7maps-7b.txt`:

```
=== BREAK: A rows:80 sent:1 recv:0 applied:0 fail:0 live:1 fall:0 pending:0 echo:0
=== BREAK: B rows:128 sent:0 recv:5 applied:1 fail:0 live:1 fall:0 pending:0 echo:0
=== BREAK: PASS peer break 75e02c2e BP_VFX_Breakable_Cart_02_A_C
               BP_VFX_Breakable_Cart_02_A_C_UAID_…_2105289101_2105289102 (FilteredHit)
=== BREAK: PASS
RESULT: PASS
```

(the console wraps that verdict line; `coop\result_break.txt` on the receiver holds it whole —
160 bytes, ending in `(FilteredHit)`. `live:1 fall:0` = the game's own entry did the work, no
fallback.)

All other verdicts in the same run are PASS: SHELL, WEAPON, PUPVIS, LOOK, ANIM, DAMAGE,
DEATH, WORLD, PROGRESS, QUEST, QUEST2, LIFT, CHEST, STORE_A2, SPAWN, MIRROR, PEERHIT,
AITARGET, STREAM, NPCFAR, BREAK, ZONE, PEERHP, PEERDEAD, A, B. No
`has no parameter` / `faulted` line in either native log.

`break-lab.ps1` run (the visual one) — both clients broke the **same three** barrels through
the game's own entry, and neither fell back:

```
[02:17:47][A] break 8f4d91cb …: FilteredHit(FilteredHit(Impulse,Location,Instigator,GenerateFieldsOnHit)) ok=true
[02:17:47][A] break 8f4d91cb … confirmed broken via FilteredHit
[02:17:47][A] break local 8f4d91cb … seq=1 at=(45144 -131180 -41966) -> published
[02:17:48][A] break local b76e4067 … seq=2 at=(45205 -131294 -41960) -> published      (chain reaction)
[02:17:48][A] break local 3ce76897 … seq=3 at=(45077 -131294 -41978) -> published      (chain reaction)
[02:17:48][B] break peer 8f4d91cb kind=B seq=1 at=(45144 -131180 -41966) imp=200
[02:17:48][B] break verdict: PASS 8f4d91cb BP_VFX_Breakable_Wooden_Barrel_a_Closed_01_C … (FilteredHit)
[02:17:48][B] break 8f4d91cb … confirmed broken via FilteredHit
[02:17:48][B] break peer b76e4067 … imp=0     /  seq=3 …
[02:17:49][B] break b76e4067 … confirmed broken via FilteredHit
=== brk A rows:5 sent:3 recv:12 applied:2 fail:0 live:3 fall:0 pending:0 echo:0
=== brk B rows:5 sent:2 recv:13 applied:3 fail:0 live:3 fall:0 pending:0 echo:0
```

`live:3 fall:0` on **both** sides = three real game breaks, zero fallbacks. The two extra
events are the game's own chain reaction (one fracture's field/force broke its neighbours);
the receiver broke those too, through the same call, instead of hiding them.

## 7b.4 Screenshots (LOOKED AT, not assumed)

Folder `G:\Freespace\shell_coop\local\lab\20261026-021547-r7maps-break4\`:

| File | What is in it |
|---|---|
| `contact-A-break.png` (sheet, frames 1-8 of `A-break01..08.png`) | **A (=breaker):** frame 1 a **wooden barrel standing** in the left of frame next to the cart; frames 2-8 the barrel is **gone with a dust flash and scattered planks/debris** where it stood |
| `contact-B-break.png` (sheet, frames 1-8 of `B-break01..08.png`) | **B (=receiver):** frames 1-2 **two intact barrels**; frame 3 the barrels **shatter into wooden staves thrown into the air** (the fracture itself is in frame); frames 4-8 the staves come down and settle into a debris pile |
| `A-before1/2.png`, `B-before1/2.png` | the same two views just before the break |
| `B-zoom.png` | 2× crop of `B-break01` vs `B-break03`: two intact banded barrels → the same spot exploding into long wooden staves in mid-air |
| `A-after.png`, `B-after.png` | settled state after the burst |
| `probe_A.log`, `probe_B.log` | the log lines quoted above |
| `result_break_B.txt` | `PASS peer break 8f4d91cb BP_VFX_Breakable_Wooden_Barrel_a_Closed_01_C … (FilteredHit)` |
| `sync_A.txt`, `sync_B.txt` | the `brk=…` counters |

Both cameras were put on the barrel deterministically: each pawn was teleported 450 uu from
the barrel **along its own yaw** (`A -> 44893 -130821 -41962`, `B -> 44893 -130821 -41911`,
both yaw −55°), which is what finally framed the object — the earlier rounds failed because
the pawns were standing 91 uu from it (i.e. under the camera) or the burst was taken after
the camera had swung into the cave wall.

## 7b.5 Still open / honest notes

1. **Debris pieces are not identical.** The fracture is Chaos-generated per client, so the
   shards differ (the owner explicitly allowed this). What matches is the *kind* of break and
   the moment: both sides run `FilteredHit` with the same point and impulse, so the debris
   comes out of the same place at the same strength.
2. **`BP_BreakableWall_C` is still unproven live** — no destructible wall exists in the hub
   (`wall:0` on both clients). Its path is `SpartaApplyHit` (walls have no `FilteredHit`)
   plus `UnlockTraversals` + the store-B record; the probe is `brkstate`/`brkcall
   SpartaApplyHit` on a wall in a map that has one, then a reload.
3. **`SpartaApplyHit` is called with `Payload = null`.** The BP VM reads a missing object as
   zero, which is safe, but a real payload would carry break damage and could break a barrel
   that `FilteredHit` cannot (none was found, so this is a spare path, not a known gap).
4. **Sound is not verified separately** — the trace shows the BP's break graph running
   (`ExecuteUbergraph_BP_VFX_Breakable` after `OnChaosBreakEvent`), and the harness runs the
   clients with `-nosound`. The owner should confirm the audio by ear once.
5. The impact **direction** sent is a coarse normal (receiver uses +Z, i.e. "hit from above")
   because only the point and impulse are on the wire; a direction would be one more vector
   and is only worth adding if the debris cone visibly points wrong.
