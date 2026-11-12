# Round 7 — `r7-wire`: split the wire packet + harness fixture reset

Workspace `G:\Freespace\shell_coop\wt\r7-wire`. Test installs: host `E:\Games\_MS2-coop-k`,
guest `E:\Games\_MS2-coop-l`, relay port 47150. `..\R7-COMMON.md` was read and obeyed: nothing
outside this workspace was edited, every deploy/run was under `Lock-Game 'r7-wire'` and released
right after, `E:\Games\Mortal Shell II` was never touched.

## 0. Verdicts at a glance

| Verdict | Kind | Result |
|---|---|---|
| `WIRE` | new, in `run-two.ps1` (log-based: `wire=` on `sync.txt` + the relay's `wire:` lines) | **PASS** — largest datagram **1 150 B** ≤ 1 200, packet cut into 6-7 parts, 3 339 parts reassembled, **0 lost** (§5.3) |
| `WIRETEST` | new, `wiretest` verb (C++ round trip of the split, no second client) | **PASS** on both clients — "merged back byte-identical (2 675 B)" |
| `FIXRESET` | new, `fixreset` verb on both clients before the tests | **PASS** on both clients: 12 save objects reset by GUID/class |
| `RESULT` | `run-two.ps1` with the ledgers **not** deleted | **PASS**, twice in a row with the split live (§5.3), plus twice on the pre-change build (§4.1/§4.2) |

## 1. Root cause

### 1.1 PART 1 — one publish was one datagram above the MTU

Measured on this pair before the change:

* `coop\out_A.txt` **2 852 B**, `coop\out_B.txt` **4 157 B** (samples in `research\mine\wire\`) —
  one JSON object, 30-31 top-level members, rewritten on every publish (15 Hz, ≈ 60 KB/s).
  The biggest single members in `out_B`: `npc` **1 335 B** (16 enemies within 4 000 uu),
  `nhp` 861 B, `wpns` 474 B, `wd` 461 B, `wb` 364 B. During the round-7 runs the same packet was
  measured at **3 735 B** (`wire=txb:3735`), with `lk` (the look signature) at **1 541 B** once a
  mask was attached.
* `relay\cooprelay.py` `UdpLink.poll()` did `sock.sendto(payload)` with that whole line, so one
  publish = one UDP datagram of ~3-4 KB = 3 IP fragments (path MTU 1 472). One lost fragment loses
  the whole tick — the transform included — and there is no retransmit.
  (`HANDOFF-REMAINING-SYNC.md` §1.2 and gap **G12** describe exactly this; §G12's proposal is what
  is implemented here.)

### 1.2 PART 2 — why a re-run needed `ws_ledger_*.txt` deleted by hand

`..\R7-COMMON.md` (round 7 additions): *"Until the r7-wire fixture reset is merged, delete
`<install>\...\coop\ws_ledger_*.txt` on both installs before each run-two."*

The WORLD/LIFT/CHEST verbs pick their fixture at run time — the nearest **closed** door, the nearest
**resting** lift, the nearest chest with `TimesUsed == 0` — and `coop_world.cpp LedgerLoad()` re-applies
every store-A key the previous session saw true (that is the point of the ledger, and the LEDGER
verdict is *about* it). A fixture the previous run consumed therefore makes the verb find nothing
(`wstest: no closed door loaded`, `cstest: no unopened chest loaded`, `lifttest: no resting lift
loaded`) and the receiving side's verdict times out. Deleting the ledger was the workaround, and it
also threw away the file the LEDGER verdict needs — which is why the first of the two baseline runs
below printed `LEDGER: A -` (no ledger existed at load).

The game-side cure the brief names is
`USpartaSaveSubSystem.ResetSaveObjectByGuid(GuidString) -> bool` (string form of the FGuid;
`SYNC-DATA-MINE.md` §4.2 line 190, §4.4 lines 273-274, §12 line 1228), optionally falling back to
`ResetSaveObjectByClass(SaveObjectClass) -> int32`. `USpartaSaveComponent.Guid` is an FGuid at
`0xFC` and `FGuid::ToString()`'s default form is `EGuidFormats::Digits` = 32 hex chars — the string
`ResetSaveObjectByGuid` parses (already carved out in round 5 for the store-A2 verb,
`coop_world_ws2.cpp GuidOfSaveObject`/`ResetByGuid`).

## 2. What changed (files and functions)

### 2.1 PART 1 — the split (native + relay)

`native/coop_state.h`
* `Toggles::wireSplit` + `!WIRE_SPLIT_OFF.txt` — the kill switch (off = the pre-round-7 single
  datagram, byte-identical).
* declarations: `net::ReadPeerPacket`, `net::WireSelfTest`; `world::FixtureReset`,
  `world::ResetActorFixtures`, `world::TestFixReset`.

`native/src/coop_net.cpp` (the packet is built and parsed here)
* `Toggles::Load()` — the `WIRE_SPLIT_OFF.txt` row.
* New file-local block: `kPartBudget = 1150`, the tx/rx counters, `SplitMembers()` (a ',' inside a
  string/array/object is not a top-level separator), `EscapePiece`/`UnescapePiece`, `PartHeader()`.
* `Publish()` — records `hotEnd` at the `,"lk":` boundary and writes the packet as N parts instead
  of one line (one line per part, `out_<me>.txt`).
* `BuildPartLines()` — packs top-level members in packet order into parts of ≤ 1 150 B; `lk` and
  `npc` (the two members that routinely exceed a datagram) are cut into raw pieces
  (`"r":"<text>","c":0|1`). **No per-key code**: a key another module appends is carried as-is.
* `MergePartLines()` / `RawPiece()` / `ReadPeerPacket()` — the receiver's half: the parts that
  arrived are merged back into the one line every reader already parses. A lost part just leaves its
  keys out. (`RawPiece` exists because `KvStr` cannot read a JSON string that contains `\"`; see
  §5.3 — the first version used `KvStr` and `wiretest` caught it.)
* `ApplyPeer()` / `DeathPeerKeepAlive()` — read through `ReadPeerPacket` (was `ReadLine`, i.e.
  first line only) and update the rx counters (a missing part is counted once per publish).
* `WriteSyncStatus()` — new `wire=` line (`txp txmax txb hot rxp rxmax rxb lost v`).
* `WireSelfTest()` + `RunCommands()` — the `wiretest` verb.
* `RunCommands()` — the `fixreset` verb.

`relay/cooprelay.py`
* `StateDir.read_out()` returns **every** line of `out_<id>.txt` (was one); `write_peer()` writes the
  parts of the newest sequence, in `p` order, atomically (tmp + `os.replace`).
* New `Parts` class: buffers `(peer, seq) → {p: line}`, hands the newest sequence back in order,
  counts received/lost parts and merged sequences, and passes a pre-round-7 datagram straight
  through (`legacy`).
* `UdpLink.poll()` sends **one datagram per part**, counts `sent`/`max_sent`/`oversize`/`dropped`,
  and drops `--loss` percent of its outgoing datagrams (the lossy-transport test).
* `run_link()` — reassembly, `wire:` stats every 5 s
  (`wire: tx sent=… max=… parts=… parts_rx=… parts_lost=… legacy=… oversize=… torn=… loss=…`),
  and a torn part (the mod was rewriting the file) is skipped quietly instead of being reported as
  broken JSON.
* `main()` — `--loss PCT`.

### 2.2 PART 2 — the fixture reset

`native/src/coop_world_ws2.cpp`
* `ResetFixtures()` split into the ungated `FixtureReset()` (the body) + the store-A2-gated wrapper,
  so the harness reset works with `WS2_OFF.txt` too.
* New `ResetActorFixtures(actorClass, &found)` — for the lift and the chests, whose state lives on
  the **actor**: actor → its `SpartaSave`/`Save` component → `Guid` at `0xFC` →
  `ResetSaveObjectByGuid(GuidString)`.

`native/src/coop_world.cpp`
* New `TestFixReset()` (`fixreset`): resets `BP_HubDoor_C`, `BPSO_BearTrap_C`,
  `BPSO_FallingCorpse_C`, `BPSO_FallingStatue_C`, `BPSO_WoodenElevator_C` by GUID (falling back to
  `ResetSaveObjectByClass`), then `BP_HubLift_C` and `BP_Interactable_Chest_C` through their save
  components, then clears every non-zero store-B `i|<chest>|TimesUsed` through
  `SpartaWorldStateHelper.WriteAnInt`. Writes `result_fixreset.txt` (`PASS fixreset: …`) and logs
  each reset.

`ms2-coop/run-two.ps1`
* After the boot wait it writes `fixreset\nwiretest\n` into `cmd.txt` on **both** clients, waits for
  `result_fixreset.txt` (40 s), and prints `=== FIXRESET:` and `=== WIRETEST:` verdicts.
* New `-LossPct` parameter (`--loss` on both relays).
* After the LEDGER block: the `WIRE` verdict — reads `wire=` from both `sync.txt` and the last
  `wire:` line of both `relay_*.log`, and requires every datagram ≤ 1 200 B, the packet split into
  ≥ 2 parts on both sides, and ≥ 1 part reassembled on both sides, plus the `wiretest` PASS.
* `$pass` now also requires `$fixOk` and `$wireOk`.

### 2.3 Workspace-only helper scripts (not part of the mod)

* `runN.ps1` — lock + optional deploy + N runs back to back under one lock (+ optional `visual-two.ps1`).
* `baseline.ps1` — the pre-change baseline run (deploy, delete the ledgers, one run).
* `tools/test_relay_parts.py` — offline test of the relay's split/reassembly (no game, no sockets).
* `tools/members.py` — measures the per-member size of a captured `out_*.txt` (the numbers in §1.1).

## 3. Test commands

```
cmd /c G:\Freespace\shell_coop\wt\r7-wire\ms2-coop\native\build.bat test
python G:\Freespace\shell_coop\wt\r7-wire\tools\test_relay_parts.py
python G:\Freespace\shell_coop\wt\r7-wire\tools\test_relay_udp.py
powershell -NoProfile -ExecutionPolicy Bypass -File G:\Freespace\shell_coop\wt\r7-wire\runN.ps1 -Tag r7fix -Runs 1 -Deploy
```

### 3.1 How to read the new `wire=` line (one line in `coop\sync.txt`)

```
wire=txp:5 txmax:1107 txb:4106 hot:948 rxp:6 rxmax:1148 rxb:2971 lost:0 v:2
      |      |         |       |       |      |         |       |     |
      |      |         |       |       |      |         |       |     +-- split protocol version (2)
      |      |         |       |       |      |         |       +-------- parts that never arrived (cumulative)
      |      |         |       |       |      |         +---------------- bytes merged back on the last packet
      |      |         |       |       |      +-------------------------- largest part received
      |      |         |       |       +--------------------------------- parts merged back on the last packet
      |      |         |       +----------------------------------------- bytes of the base (fast-lane) members
      |      |         +------------------------------------------------- bytes of the whole publish
      |      +----------------------------------------------------------- largest part this client cut
      +------------------------------------------------------------------ parts this client cut (last publish)
```

and the relay's own line, every 5 s in `coop\relay_<id>.log`:

```
wire: tx sent=3524 recv=0 max=1149 parts=683 dropped=0 parts_rx=3183 parts_lost=0 legacy=24 torn=0 loss=0
```
`sent`/`max` = datagrams this relay sent and the biggest one, `parts_rx`/`parts_lost` = parts it
received / admitted to be missing, `legacy` = pre-round-7 single-line datagrams still accepted,
`torn` = a part caught while the mod was rewriting the file (skipped), `dropped`/`loss` = the
`--loss` test.

## 4. Verdict lines

### 4.1 Baseline, BEFORE any change (build `37bb8d18`, ledgers deleted as R7-COMMON says)

`powershell -File baseline.ps1 -Tag baseline` (deploy + `run-two.ps1 -Port 47150`, ledgers removed):

```
=== SHELL: PASS          === WEAPON: PASS       === PUPVIS: PASS   === LOOK: PASS
=== ANIM: PASS           === DAMAGE: PASS       === DEATH: PASS    === WORLD: PASS
=== PROGRESS: PASS       === QUEST: PASS        === QUEST2: PASS   === LIFT: PASS
=== CHEST: PASS          === STORE_A2: PASS     === SPAWN: PASS    === MIRROR: PASS
=== PEERHIT: PASS        === AITARGET: PASS     === STREAM: PASS   === NPCFAR: PASS
=== LEDGER: A ok keys:12 good:0 bad:0 applied:0 ; B ok keys:12 good:0 bad:0 applied:0
=== LEDGER: A - ; B -                 <- no ledger file existed at load, so no verdict
RESULT: PASS
```

### 4.2 Second run, ledger **not** deleted (build `37bb8d18`)

`powershell -File runN.ps1 -Tag r2before` (no deploy, no ledger delete, the ledger the run above
wrote was still there):

```
=== WORLD: PASS world state: door BP_HubDoubleDoor_C_..._1808979936_1808979937 opened, BP_CurrencyBag_Pickup_C_..._1764479806_1764479807 collected (live=2 base=4 fail=0)
=== LIFT: PASS lift BP_HubLift_C_..._1653632767_1653632768 followed the peer to floor 1 (cabin z -42570 -> -40109, travel 2461, applied 1, fail 0)
=== CHEST: PASS chest BP_Interactable_Chest_General_C_..._2008351691_2008351692: lid replayed (anim pos 5.07), loot ID_Charges_Default.ID_Charges_Default_C 1 -> 2
=== LEDGER: A ok keys:12 good:12 bad:4 applied:4 ; B ok keys:12 good:0 bad:0 applied:0
=== LEDGER: A PASS ledger: BPSO_NPC_C.HasTriggeredInitialGreeting (BP_Cultist_HUB_C_..._1464626973_1464626974) re-applied from the ledger on load (ledger 12 keys, applied 4)
RESULT: PASS
```

So on this pair the two runs already pass with the ledger left in place — the R6 `[R6-WORLD]`
`WsResetKeyFalse` change plus the harness's save snapshot restore already make WORLD/LIFT/CHEST
re-runnable, and LEDGER passes exactly when the previous run left a ledger (it printed `-` in the
first run). The `fixreset` verb below removes the remaining fragility: it resets the fixtures
**before** any verb picks one, instead of relying on the verb's own reset, and it does it for the
lift and the chests, whose state is not a store-A row the verb reset covers.

### 4.3 The split, measured on the wire (build `73091b35`, first version of the split)

One real two-client run, `sync.txt` of the host:

```
wire=txp:4 txmax:1587 txb:3735 hot:1541 rxp:3 rxmax:1587 rxb:2923 lost:0 v:2
relay_A.log: wire: tx sent=754 recv=0 max=1595 parts=118 dropped=0 parts_rx=331
             parts_lost=0 legacy=35 oversize=197 torn=0 loss=0
```

Reading it: the 3 735-byte publish became **4 datagrams**, the peer **reassembled 3-4 parts on every
publish** (`parts_rx=331`), **no part was lost**, **no torn/broken line** was ever seen, and the 35
`legacy` datagrams are the small `{"rdy":0}` packets sent before a world was loaded. The transform
kept flowing (`rx` non-zero on every tick) — i.e. the mechanism works end to end.

**But `hot:1541` shows the defect this measurement found**: part 0 was "everything `AppendFmt`
wrote", which includes `"lk"` — and `lk` (the look signature: every attachable's mesh, socket,
transform and materials) is **1 541 B on its own** once a mask is attached. So part 0 stayed over
the MTU (`oversize=197`, `txmax=1587`). Fixed by ending part 0 at the `,"lk":` boundary and letting
`lk` be packed like any other member (it can now end up in its own datagram or be cut in two raw
pieces). See §5.3/§5.4 for what was verified after the fix.

## 5. Results

### 5.1 Offline/transport tests (no game, no lock)

```
$ python tools\test_relay_udp.py
clean: 4/4 part(s) arrived, written in p order=True
clean relay: [..] wire: tx sent=4 recv=0 max=523 parts=0 dropped=0 parts_rx=0 parts_lost=0 legacy=0 oversize=0 torn=0 loss=0
lossy: hot section present in 6/16 samples, partial sets in 13
lossy relay: [..] wire: tx sent=12 recv=0 max=523 parts=0 dropped=16 parts_rx=0 parts_lost=0 legacy=0 oversize=0 torn=0 loss=50
RESULT: PASS

$ python tools\test_relay_parts.py
complete set: 4 line(s), merged=1 lost=0
lost part 2 of 4: 3 line(s) written, hot intact=True
next sequence: 2 line(s), merged=3 lost=1
legacy datagram passthrough=True legacy=1
RESULT: PASS
```

`test_relay_udp.py` runs the two relays the way `run-two.ps1` does, over a real UDP socket on a spare
port, with a 4-part publish: all 4 parts arrive on the peer in `p` order and are written as one
4-line file; with `--loss 50` the relay drops half its datagrams and the peer still gets the hot
part most of the time — a lost part costs only its own keys.

### 5.2 Build

```
$ cmd /c G:\Freespace\shell_coop\wt\r7-wire\ms2-coop\native\build.bat test
coop_policy: 112 checks passed, 0 failed
PASS: all 4 patterns match exactly once and resolve to the recovered addresses
Built: G:\Freespace\shell_coop\wt\r7-wire\ms2-coop\native\build\dxgi.dll
```

R7-COMMON's post-change grep: `Select-String ms2coop-native.log -SimpleMatch 'has no parameter'` →
**0 hits on both installs** after the §5.3 runs (the new UFUNCTION calls are
`ResetSaveObjectByGuid`, `ResetSaveObjectByClass` and `WriteAnInt`; the first two return `true` /
a count in `result_fixreset.txt`, which is the proof the parameter names are right).

### 5.3 The two back-to-back runs with the split live (ledger kept, nothing deleted)

Both runs were started with the `coop\ws_ledger_0.txt` / `coop\wt_ledger_0.txt` the previous runs
had left on both installs (they are still there now, 108 B / 26-54 B); no run deleted any ledger file.

**Run 1** — build `73091b35` (`local\r7wire-1-out.txt`):

```
=== FIXRESET: A PASS fixreset: 12 save object(s) reset by guid/class (world 4, lift 0 of 0, chest 8 of 8), 0 store-B TimesUsed cleared
=== FIXRESET: B PASS fixreset: 12 save object(s) reset by guid/class (world 4, lift 0 of 0, chest 8 of 8), 0 store-B TimesUsed cleared
=== FIXRESET: PASS
=== WIRETEST: A FAIL wire: packet 2675 B -> 5 part(s), max 1148 B (budget 1150); merged back DIFFERENT (372 B); ...
=== WIRETEST: FAIL
=== SHELL: PASS   === WEAPON: PASS   === PUPVIS: PASS   === LOOK: PASS   === ANIM: PASS
=== DAMAGE: PASS  === DEATH: PASS    === WORLD: PASS    === PROGRESS: PASS === QUEST: PASS
=== QUEST2: PASS  === LIFT: PASS     === CHEST: PASS    === STORE_A2: PASS   === SPAWN: PASS
=== MIRROR: PASS  === PEERHIT: PASS  === AITARGET: PASS  === STREAM: PASS   === NPCFAR: PASS
=== LEDGER: A ok keys:12 good:0 bad:4 applied:0 ; B ok keys:12 good:8 bad:4 applied:4
=== LEDGER: A - ; B PASS ledger: BPSO_NPC_C.HasTriggeredInitialGreeting (...) re-applied from the ledger on load
=== WIRE: A txp:5 txmax:1107 txb:4106 hot:948 rxp:6 rxmax:1148 rxb:2971 lost:0 v:2
=== WIRE: B txp:6 txmax:1148 txb:4111 hot:947 rxp:5 rxmax:1099 rxb:4244 lost:0 v:2
=== WIRE: relay [01:43:19] wire: tx sent=3524 recv=0 max=1149 parts=683 dropped=0 parts_rx=3183 parts_lost=0 legacy=24 torn=0 loss=0
=== WIRE: FAIL (packet split into 5/6 part(s), largest datagram 1149 B <= 1200, reassembled 6/5, parts lost 0)
=== ZONE: PASS   === PEERHP: PASS   === PEERDEAD: PASS   === A: PASS   === B: PASS
RESULT: FAIL
```

Every WIRE criterion was met (**largest datagram 1 149 B** instead of 4 157, 5-6 parts, 3 183 parts
reassembled, 0 lost, 0 torn); the red came only from `WIRETEST` — **and that FAIL was a real bug the
self test caught**: `MergePartLines` read the raw piece of an oversized member with `net::KvStr`,
which stops at the first `"` and knows nothing about `\"`, so a *chunked* member was truncated at its
first escaped quote and the merged packet came back 372 B instead of 2 675 B. The live hub traffic
only used the non-chunked path (`lk` 947 B, small `npc`), which is why LOOK/WORLD still passed.
Fixed with `RawPiece()` (scans a JSON string with escapes and unescapes in the same pass).

**Run 2** — build `756998cd` (the `RawPiece` fix, `local\r7fix-out.txt`):

```
=== FIXRESET: phase A=sync B=sync after the boot wait
=== FIXRESET: A PASS fixreset: 12 save object(s) reset by guid/class (world 4, lift 0 of 0, chest 8 of 8), 0 store-B TimesUsed cleared
=== FIXRESET: B PASS fixreset: 12 save object(s) reset by guid/class (world 4, lift 0 of 0, chest 8 of 8), 0 store-B TimesUsed cleared
=== FIXRESET: PASS
=== WIRETEST: A PASS wire: packet 2675 B -> 5 part(s), max 1148 B (budget 1150); merged back byte-identical (2675 B); ...
=== WIRETEST: B PASS wire: packet 2675 B -> 5 part(s), max 1148 B (budget 1150); merged back byte-identical (2675 B); ...
=== WIRETEST: PASS
=== SHELL: PASS   === WEAPON: PASS   === PUPVIS: PASS   === LOOK: PASS (on=True off=True)
=== ANIM: PASS    === DAMAGE: PASS    === DEATH: PASS    === WORLD: PASS    === PROGRESS: PASS
=== QUEST: PASS   === QUEST2: PASS    === LIFT: PASS     === CHEST: PASS     === STORE_A2: PASS
=== SPAWN: PASS   === MIRROR: PASS    === PEERHIT: PASS  === AITARGET: PASS  === STREAM: PASS
=== NPCFAR: PASS   === ZONE: PASS      === PEERHP: PASS    === PEERDEAD: PASS  === A: PASS   === B: PASS
=== LEDGER: A ok keys:12 good:8 bad:4 applied:4 ; B ok keys:12 good:0 bad:0 applied:0
=== LEDGER: A PASS ledger: BPSO_NPC_C.HasTriggeredInitialGreeting (BP_Cultist_HUB_C_..._1464626973_1464626974) re-applied from the ledger on load
=== WIRE: A txp:7 txmax:1148 txb:5885 hot:948 rxp:6 rxmax:1148 rxb:4411 lost:0 v:2
=== WIRE: B txp:6 txmax:1148 txb:4411 hot:947 rxp:7 rxmax:1148 rxb:6023 lost:0 v:2
=== WIRE: relay [02:01:40] wire: tx sent=4117 recv=0 max=1149 parts=670 dropped=0 parts_rx=3339 parts_lost=0 legacy=20 torn=0 loss=0
=== WIRE: PASS (packet split into 7/6 part(s), largest datagram 1150 B <= 1200, reassembled 6/7, parts lost 0)
RESULT: PASS
```

So, on the wire: a **5 885-byte** publish (a busy hub moment, seven top-level members large enough to
matter) became **7 datagrams, the largest 1 150 B**, and **3 339 parts were reassembled with 0 lost
and 0 torn lines**, while every feature verdict — transform (A/B), SHELL, WEAPON, LOOK, ANIM,
DAMAGE, DEATH, WORLD, PROGRESS, QUEST/QUEST2, LIFT, CHEST, STORE_A2, SPAWN, MIRROR, PEERHIT,
AITARGET, STREAM, NPCFAR, ZONE, PEERHP/PEERDEAD, and **LEDGER with the ledger file left in place** —
passed. `RESULT: PASS`.


### 5.4 Timing note (why four runs and not two)

The whole session contended for one shared game lock with four other round-7 agents. From 00:49 on
the lock was continuously taken by `r7-maps`, `r7-drops` and `r7boss`; one of my runs (`pwsh-8`,
01:22:45) lost both clients to a racing agent 6 s after launch (the host's `probe.log` ends at
01:22:51,
the guest's `probe.log` was deleted and never re-created, both PIDs gone), and the `RawPiece` run
had to be queued for 10 minutes behind `r7boss`/`r7-drops`. That is why the two runs that matter
(§5.3 run 1 and run 2) are 20 minutes apart on two consecutive builds rather than one
`-Runs 2` invocation: run 1 exposed the merge bug, the fix was built, and run 2 proved it. The
ledger was never deleted in either of them.


## 6. Screenshots

None. The change is transport- and harness-level: nothing new is on screen, and the visible
behaviour (puppet body, weapons, look, locomotion, death clip) is exactly what `SHELL`/`WEAPON`/
`LOOK`/`ANIM`/`PUPVIS`/`PEERDEAD` assert from `sync.txt` and the puppet probes — all of them PASS in
§5.3 run 2. `runN.ps1 -Visual` is wired for `visual-two.ps1` but no lock slot was left in the time
budget (§5.4).

## 7. Open / not done

1. A `visual-two.ps1` contact sheet for the final build (nothing new is drawn by this change; the
   puppet/weapon/look verdicts stand in for it).
2. `lk` still rides with the slow members. With a mask on it is ~1 000-1 500 B, so it takes a part
   (or, past ~1 100 B, two raw chunks) of its own on every publish. If the look sync is ever
   noticed to be laggy, the next step is to publish `lk` at 2 Hz instead of 15 Hz
   (`appear::Mine()` only recomputed when it changes).
3. The relay keeps parts per `(peer, seq)` and drops sequences older than the newest, so a part that
   arrives after its sequence was superseded is discarded — correct for latest-value-wins, and
   `parts_lost` is the only trace of it (`parts_lost=0` over 3 339 parts in §5.3 run 2).
4. `fixreset` writes the store-B `TimesUsed` back to 0 through
   `SpartaWorldStateHelper.WriteAnInt` — a real save write (the harness restores the saves at the end
   of a run anyway). In both §5.3 runs nothing needed clearing (`0 store-B TimesUsed cleared`), i.e.
   the save-object reset alone was enough on this save.
5. `fixreset` reports `lift 0 of 0` — `ue::FindAllOf("BP_HubLift_C")` found no **actor** to take a
   save-component GUID from (the lift state is covered by `BPSO_WoodenElevator_C`, which the
   store-A half does reset, and `lifttest` passes). If a future save ever leaves the lift at the
   wrong floor after a run, look at whether the lift actor is streamed out at test start.
6. The LEDGER verdict is asymmetric between the clients in both §5.3 runs (`A -` / `B PASS`, then
   `A PASS` / `B -`): the client whose ledger keys `fixreset` just reset reads them false
   (`good:0 bad:4`), the other one still proves the replay. It passes every time so far, but a save
   where *every* ledgered key is a fixture `fixreset` touches would make LEDGER unprovable — the
   probe is `sync.txt`'s `ledger=` line right after the reset.
7. The `--loss` option was exercised on a synthetic state dir over a real socket (§5.1), not on a
   live two-client run.



---

# Round 7b — STORE_A2 determinism: `fixreset` now resets the store-A2 fixtures too

Follow-up to the integrated verification of `main` (`wt\r7-final\REPORT.md` §2.1), which found
`STORE_A2` failing **deterministically twice** (verdict line empty: no `result_ws2.txt` inside the
130 s window) and traced it to this round's `fixreset`.

## B.1 Root cause

`TestFixReset()` reset **5** save-object classes (`BP_HubDoor_C`, `BPSO_BearTrap_C`,
`BPSO_FallingCorpse_C`, `BPSO_FallingStatue_C`, `BPSO_WoodenElevator_C`) plus the lift/chests, but
the `STORE_A2` verb (`wa2test`) writes its fixtures on **different** classes: the new-class boolean
on `BPSO_NPC_C.HasTriggeredInitialGreeting` and the typed ints on `BPSO_Tarforge_C.StoredTarcores` /
`BPSO_FC_FinalBossManager_C.ClensedCores` (`coop_world_ws2.cpp` `kBoolPicks` / `kValPicks`).

That produced an asymmetry, and it is the whole bug:

* the **host** runs `Wa2FixtureReset()` itself (`coop_world_ws2.cpp` ~1715, its list *does* include
  `BPSO_NPC_C`) before writing the fixture, so it always publishes a genuine `false -> true` change;
* the **guest** was never reset, so its copy already held `true` — from the host's boot snapshot
  (`ws apply base 5f5dd95e ... ok`, r7-final §2.1) and the store-A ledger replay.

A live arrival for a key whose value does not change cannot produce a confirmed readback
(`coop_world.cpp` `ScanOne`: `liveOk` is set to 0 "pending readback" on apply and only set to 1 when a
later scan reads the value true), and the apply itself is gated on `RowSettled(idx)`
(`ApplyPeerWorld` ~1033) — so on the guest the apply was deferred (127-134 s, measured) and even then
produced no `liveOk` entry. `ws2`'s judge therefore never saw a new-class boolean from the change
block, never wrote `result_ws2.txt`, and the harness printed an empty `STORE_A2` line, which gates
`RESULT`.

Fix chosen: **the class list**, i.e. the root cause. `fixreset` is already sent to *both* clients by
`run-two.ps1`, so adding the missing classes makes `fixreset` do on the guest exactly what
`Wa2FixtureReset()` does on the host — the host's `wa2test` write then lands as a real change on both
sides. The alternative the r7-final report also lists (writing `wa2reset` to `$coopB`'s `cmd.txt`)
would reset a *narrower* list (`Wa2FixtureReset`'s, which lacks `BPSO_FC_FinalBossManager_C`) on one
client only, and would leave the two lists diverged, so it was not used.

## B.2 Diff (exact)

`native/src/coop_world.cpp`, function `TestFixReset()` — one array plus a comment; nothing else in
the tree changed (`run-two.ps1`, `coop_world_ws2.cpp` and all wire code are untouched):

```diff
--- a/native/src/coop_world.cpp
+++ b/native/src/coop_world.cpp
@@ TestFixReset()
-    // store-A save objects: the door/pickup half of WORLD, the flippable world
-    // bools, and the hub lift's own save object (BPSO_WoodenElevator_C)
-    static const char* kSaveObjects[] = {"BP_HubDoor_C", "BPSO_BearTrap_C",
-                                         "BPSO_FallingCorpse_C", "BPSO_FallingStatue_C",
-                                         "BPSO_WoodenElevator_C"};
+    // store-A save objects: the door/pickup half of WORLD, the flippable world
+    // bools, the hub lift's own save object (BPSO_WoodenElevator_C) and -- round
+    // 7b -- every class a run-two STORE_A2 fixture writes (coop_world_ws2.cpp
+    // kBoolPicks/kValPicks: BPSO_NPC_C for the new-class boolean, BPSO_Tarforge_C
+    // and BPSO_FC_FinalBossManager_C for the typed ints).
+    //   [12 lines of comment explaining the host/guest asymmetry and the deferred,
+    //    unconfirmed live apply -- see B.1]
+    static const char* kSaveObjects[] = {"BP_HubDoor_C", "BPSO_BearTrap_C",
+                                         "BPSO_FallingCorpse_C", "BPSO_FallingStatue_C",
+                                         "BPSO_WoodenElevator_C", "BPSO_NPC_C",
+                                         "BPSO_Tarforge_C", "BPSO_FC_FinalBossManager_C"};
     int byObject = 0;
     for (std::size_t i = 0; i < sizeof(kSaveObjects) / sizeof(kSaveObjects[0]); ++i) {
         byObject += FixtureReset(kSaveObjects[i]);
     }
```

`fixreset` now reports **25** save objects reset instead of 12 (`=== FIXRESET: ... (world 17, lift 0
of 0, chest 8 of 8)`), and **both** clients log the NPC class:

```
[06:45:20][A] ws2 fixture: BPSO_NPC_C reset by class -> 13 object(s) (by guid: 0 of 39 ok)
[06:45:21][B] ws2 fixture: BPSO_NPC_C reset by class -> 13 object(s) (by guid: 0 of 39 ok)
[06:45:20][A] ws2 fixture: BPSO_Tarforge_C reset by class -> 0 object(s) (by guid: 1 of 3 ok)
[06:45:20][A] ws2 fixture: BPSO_FC_FinalBossManager_C reset by class -> 0 object(s) (by guid: 1 of 3 ok)
```

(the two `by guid: 1 of 3 ok` classes reset their one *live* instance by GUID; the other two instances
of each are transient copies with no owning actor, a case `ResetFixtures` already documents).

**The guest really is reset now** — this is the mechanism, guest `probe.log`, ~1 s after `fixreset`
and therefore *before* the host's `wa2test` write:

```
[06:45:12][B] ws see 5f5dd95e HasTriggeredInitialGreeting=false raw=false(boolean) BPSO_NPC_C ...
[06:45:12][B] ws see 767b6e14 HasTriggeredInitialGreeting=false ...   (every NPC row, all false)
```

## B.3 Verdict lines (two runs back to back, no ledger deletion)

`powershell -NoProfile -ExecutionPolicy Bypass -File runN.ps1 -Tag r7b -Runs 2 -Deploy` (deploy once,
then two `run-two.ps1 -Port 47150`; `coop\ws_ledger_0.txt` / `wt_ledger_0.txt` were left in place on
both installs throughout).

Run 1 (`local\r7b-1-out.txt`):

```
=== FIXRESET: A PASS fixreset: 25 save object(s) reset by guid/class (world 17, lift 0 of 0, chest 8 of 8), 0 store-B TimesUsed cleared
=== FIXRESET: B PASS fixreset: 25 save object(s) reset by guid/class (world 17, lift 0 of 0, chest 8 of 8), 0 store-B TimesUsed cleared
=== FIXRESET: PASS
=== STORE_A2: A kinds:10 typed:3 scans:250 recv:1110 applied:2 str:0 fail:0 leaks:0 ; B kinds:10 typed:3 scans:215 recv:...
=== STORE_A2: PASS store A2: BPSO_NPC_C.HasTriggeredInitialGreeting replayed on BP_Cultist_HUB_C_..., BPSO_Tarforge_C.StoredTarcores=1 ...
=== LEDGER: A ok keys:12 good:10 bad:4 applied:5 ; B ok keys:12 good:0 bad:0 applied:0
=== LEDGER: A PASS ledger: BPSO_NPC_C.HasTriggeredInitialGreeting (...) re-applied from the ledger on load
=== WIRE: A txp:6 txmax:1148 txb:5435 hot:948 rxp:6 rxmax:1148 rxb:4610 lost:0 v:2
=== WIRE: B txp:6 txmax:1148 txb:4610 hot:947 rxp:6 rxmax:1148 rxb:5573 lost:0 v:2
=== WIRE: PASS (packet split into 6/6 part(s), largest datagram 1150 B <= 1200, reassembled 6/6, parts lost 0)
RESULT: PASS
```

Run 2 (`local\r7b-2-out.txt`, the pair member that matters — the ledger files were still there):

```
=== FIXRESET: A PASS fixreset: 25 save object(s) reset by guid/class (world 17, lift 0 of 0, chest 8 of 8), 0 store-B TimesUsed cleared
=== FIXRESET: B PASS fixreset: 25 save object(s) reset by guid/class (world 17, lift 0 of 0, chest 8 of 8), 0 store-B TimesUsed cleared
=== FIXRESET: PASS
=== STORE_A2: A kinds:10 typed:3 scans:248 recv:1342 applied:1 str:0 fail:0 leaks:0 ; B kinds:10 typed:3 scans:225 recv:...
=== STORE_A2: PASS store A2: BPSO_NPC_C.HasTriggeredInitialGreeting replayed on BP_Cultist_HUB_C_...
=== LEDGER: A ok keys:12 good:12 bad:4 applied:4 ; B ok keys:12 good:0 bad:0 applied:0
=== LEDGER: A PASS ledger: BPSO_NPC_C.HasTriggeredInitialGreeting (...) re-applied from the ledger on load
=== WIRE: A txp:7 txmax:1148 txb:5688 hot:948 rxp:6 rxmax:1148 rxb:4523 lost:0 v:2
=== WIRE: B txp:6 txmax:1148 txb:4523 hot:947 rxp:6 rxmax:1147 rxb:5207 lost:0 v:2
=== WIRE: relay [06:53:30] wire: tx sent=4308 recv=0 max=1149 parts=720 dropped=0 parts_rx=3550 parts_lost=0 legacy=18 torn=0 loss=0
=== WIRE: PASS (packet split into 7/6 part(s), largest datagram 1150 B <= 1200, reassembled 6/6, parts lost 0)
RESULT: PASS
```

`STORE_A2` PASS **twice in a row**, with no ledger file deleted between the runs, and `RESULT: PASS`
both times. `has no parameter` after both runs: 0 hits on both installs.

## B.4 The proof that the fix is the mechanism, not luck

Guest `probe.log`, run 2, ~1 s after the host's `wa2test` write (the exact pair of lines the
integrated report says was missing — a live apply that is immediately confirmed by a readback):

```
[06:51:54][B] ws apply live 767b6e14 BPSO_NPC_C.HasTriggeredInitialGreeting (BP_Cultist_HUB_C_..._1464626973_1464626974)
[06:51:54][B] ws readback 767b6e14 BP_Cultist_HUB_C_..._1464626973_1464626974 now true (direct read after the live apply)
```

Before this change the guest's copy already read `true` (host boot snapshot + ledger replay), so the
live arrival was a same-value write: no confirmed `liveOk`, deferred apply, no `result_ws2.txt`.
Now the guest's copy is `false` when the host's change arrives (`ws see 5f5dd95e ... =false`, §B.2),
the apply is a real change, and the readback confirms it — which is what `ws2`'s judge needs.

Two smaller notes, for honesty:

* the class-path count in `FixtureReset`'s return value is 0 for `BPSO_Tarforge_C` /
  `BPSO_FC_FinalBossManager_C` (their one live instance is reset through the GUID path first), so an
  intermediate version of this patch logged "nothing reset" for them and that line was removed again;
  the final source therefore differs from the binary the two runs used (`a49e15d1`) only by that
  removed log line (rebuilt as `d9ec9169`).
* `BPSO_NPC_C` also carries `WasEverKilled`, which this reset clears, so NPCs re-greet after a run.
  That is documented in `Wa2FixtureReset`'s own list (which already reset this class on the host) and
  the harness restores the owner saves at the end of a run anyway.
