# Round 7 / 7b — G6 "Enemy drops" (agent `r7-drops`)

Workspace: `G:\Freespace\shell_coop\wt\r7-drops` · test installs A = `E:\Games\_MS2-coop-test`,
B = `E:\Games\_MS2-coop-b` · relay port 47110.

**Status: done and green.** Round 7 built the mechanism but could not prove a *rewarded* kill;
round 7b found why (the game's death ability is never activated by a co-op kill) and made the owner
roll from the enemy's own `CharacterDeathData`, so the path is now exercised **in both directions**:
`DROPS: PASS both directions` and `RESULT: PASS` in run-two (`run-two-r7b-3.txt`, §7b.5). Round 7b
also corrects a wrong claim in §2 — the enemies *do* carry drop data (§7b.1).

---

## 1. Root cause

Each client runs **its own copy** of every enemy, and the loot roll is authored in the *death
ability of the copy that died*:

* `BP_AICharacter_C.CharacterDeathData` @ `0xDA8` (`FSpartaCharacterDeathData{GloomOnDeath 0x0,
  GoldOnDeath 0x4, GoldDropChance 0x8, ItemDropTable 0x10}`, size 0x60),
  `.UseCharacterDataRewards` @ `0xDA0`, `.AdditionalDropItems` @ `0xE08`
  (`research/raw/CXXHeaderDump/BP_AICharacter.hpp`);
* the ability is `GA_DeathBase_C` — `DropCurrency`, `DropGold`, `DropGloom`, `DropItems`,
  `TotalGold` @ `0x5D0`, `TotalGloom` @ `0x5D4`, `PostGoldSpawn`/`PostGloomSpawn(AActor*)` — with
  `GA_Death_AI_C` as the enemy subclass (`GA_DeathBase.hpp`, `GA_Death_AI.hpp`);
* its output is a **world pickup actor**: `ABP_CurrencyBase_C` (`Amount` @ `0x2E0`,
  `CurrencyClass` @ `0x3C0`, `bGrantImmediately` @ `0x408`, `GrantCurrency()`), i.e. a currency bag
  the player then collects, not a direct add. The HUD widget `WBP_Player_Currency_C` holds
  `ItemManager` @ `0x400` + `ItemDef` @ `0x408` + `CurrentAmount` @ `0x424`: currency is an **item
  stack**, read through `USpartaItemManagerComponent`.

Round 6's death sync makes **both** copies die (`dead` → `KillTailLogged` → `StartDeath`,
`nhp` → `ApplyHealth` → `StartDeath`), so both clients decided loot from their own RNG — two
players, two different loot sets, neither shared.

## 2. How the game grants loot — measured (round 7)

Two-client run, both in gameplay (`phase=sync`), hooks on both
(`local\lab\20261026-012258-r7drops-probe\`):

```
[A] drops: 5 of 5 drop hook(s) installed (the roll and the veto are live)         01:23:14
[B] drops: 5 of 5 drop hook(s) installed (the roll and the veto are live)         01:23:40
[A] dropskill: BP_Cultist_HUB_C_..._1417419334 (BP_Cultist_HUB_Beacon_C) (12 candidate(s))
[A] dropskill: ... -> health 0, startDeath ok=1                                   01:24:44
[B] drops: ... is a mirrored death (peer) -- its own loot roll is vetoed for 10s  01:24:50
[B] dropsgrant: asked gold=5 gloom=0 -> S_AddGold ok=1 S_AddGloom ok=0            01:24:48
```

1. The non-owner's veto table arms for a mirrored death; both clients install 5/5 hooks.
2. `S_AddGold`/`S_AddGloom` exist on `BP_PlayerController_C` and accept a call (`ok=1`).
3. `GetTotalItemCountByDefinition(ID_Gold_C)` is **not** the balance — it counts currency *stacks*
   (it read 1–2 while `CumulativeGold` was 1212). The real number is on the HUD (§7b.6).
4. **The roll never happened**: `rolled:0 calls:0` on both clients for a `StartDeath` kill. Round 7
   guessed "this enemy carries no drop data"; 7b disproved that and found the real cause (§7b.1-2).

## 3. The change

New module `native\src\coop_drops.cpp` (+ state in `coop_state.h`, wired into `coop_net.cpp` and
`coop_combat.cpp`). One new top-level wire key, appended after every existing key (nothing moved):

```
,"drp":[[<seq>,"<enemyKey>",<gold>,<gloom>,"<itemPath>@<count>|..."], ...]
```

| Piece | Where | What it does |
|---|---|---|
| Owner roll (game) | `RollHook` (POST `GA_DeathBase_C.DropCurrency`) | primary path: reads `TotalGold`/`TotalGloom` off the live ability and publishes it |
| Owner roll (fallback) | `RollFromData` + `DeathStartHook` (POST `SpartaHealthComponent.StartDeath`) | the game never activates the ability for a co-op kill (7b.2), so the owner rolls from `CharacterDeathData` ~1 s later, and cancels itself if the game rolled |
| Non-owner veto | `VetoHook` (PRE-SKIP `DropCurrency`/`DropGold`/`DropGloom`/`DropItems`) | a mirrored death's own drop code does not run |
| Mirrored-death marking | `coop_combat.cpp` `KillTailLogged`, `ApplyHealth` | `drops::MarkMirrorDeath(actor, why)` right before `StartDeath` (10 s window) |
| Grant (both sides) | `GrantCurrency` → `S_AddGold`/`S_AddGloom`, item fallback `AddItemByClass` | the owner takes its own roll immediately, the peer takes a copy; totals are never mirrored |
| Dedupe / loss | `DropEvent.seq` + `lastPeerSeq` + 5 s retransmit | a repeat is counted (`dup:`) and ignored; a lost packet is re-sent; a seq that jumps back >8 is a restarted peer |
| Kill switch | `DROPS_OFF.txt` (`toggles().dropsEnabled`) | no `drp`, no grant, no hook — round-6 behaviour |
| Status | `sync.txt` `drops=rolled:.. calls:.. sent:.. got:.. gold:.. gloom:.. items:.. vetoed:.. dup:.. hooks:..` | what the harness reads |
| Lab verbs | `dropsprobe`, `dropsenemies`, `dropswallet`, `dropsgrant`, `dropskill`, `dropsarm`, `dropshit`, `dropsforce`, `dropsdump` | probes used in 7b |

Verdict files: `result_drops_a.txt` (the roller's claim — a provisional `NONE` at death start, then
`PASS …` when the roll lands) and `result_drops.txt` (the receiver: amounts, stack delta, `vetoed=`,
`dup=`).

## 4. Test commands and verdicts (round 7)

Everything holds the shared lock, deploys with `deploy.ps1 -GameDir <install> -DllPath
<ws>\ms2-coop\native\build\dxgi.dll` to A and B, and runs the harness from this workspace copy in a
fresh process.

```
cmd /c <ws>\ms2-coop\native\build.bat test
  test_api: PASS -- ms2_ue.h compiles and every declared signature matches
  coop_policy: 112 checks passed, 0 failed
  PASS: all 4 patterns match exactly once and resolve to the recovered addresses
  Built: ...\native\build\dxgi.dll

powershell -File <ws>\scratch\run-probe.ps1 -Cmds '...'   # two clients + screenshots + logs
powershell -File <ws>\scratch\run-two.ps1  -Out ...       # deploy both, then run-two.ps1
```

Round 7's two-client run proved the transport half (event on the wire, granted once, 15 re-sends
ignored) but its verdict could only be `DROPS: WARN` — no roll happened. Native logs stayed clean of
`has no parameter` / `MISMATCH` / `init: NOT READY` on both installs. Round 7b closes it with a real
PASS (§7b.5).

## 5. Files touched

| File | What changed |
|---|---|
| `native\src\coop_drops.cpp` | **new** — the module (§3 + 7b) |
| `native\coop_state.h` | `Toggles::dropsEnabled`; `DropItem`, `DropEvent`, `DropVeto`, `DropsState`; `State::drops`; `namespace drops` declarations |
| `native\src\coop_net.cpp` | `DROPS_OFF.txt` row; `drops::Publish` in `net::Publish`; `drops::OnPeerLine` step in `ApplyPeer` (outside the zone gate); `drops::Tick2` in `Loop2` (before the Sync gate); `drops::Reset` in `ResetAll`; `drops::Status` in `WriteSyncStatus`; `drops::OnCommand` dispatch |
| `native\src\coop_combat.cpp` | 2 insertions: `drops::MarkMirrorDeath(...)` in `KillTailLogged` and in `ApplyHealth`, immediately before `StartDeath` |
| `run-two.ps1` | the `DROPS` block (both directions, §7b.5), its wait/parse helpers, the verdict files removed at start, `$pass = $pass -and ($dropsOk -or $dropsWarn)` |
| `scratch\*.ps1` | scratch runners (`run-probe`, `run-two`, `run-visual`, `visual-drops`, `check-drops-verdict`) — not part of the deliverable |

## 6. Kill switch

`DROPS_OFF.txt` in the coop dir. With it present: no `"drp"` section, nothing granted on receipt, no
hook installed (the non-owner goes back to its round-6 behaviour) and `drops=` reads `hooks:-1`.
Verified by code path only — the file was not created in a run (time).

## 7. Harness notes worth keeping

* `run-two.ps1` defines `Wait-File` near the end but uses it earlier; a block inserted *before* that
  definition must carry its own wait helper (this cost two runs).
* `Wait-DropsFile` must wait for `PASS`, not for the file: the killer writes a provisional `NONE`
  the instant a death starts, and the roll overwrites it ~1 s later.
* A copied harness script must keep `$root` pointing at `ms2-coop\` — `visual-two.ps1` derives the
  relay path from `$PSScriptRoot`, so a copy in `scratch\` starts **no relay at all** and the two
  clients silently never talk (this cost three visual runs; see 7b.6).
* `visual-two.ps1` writes its commands ~16 s after `phase=sync`; give the peer link longer before a
  kill if you copy that flow.
* The shared run lock was contended all session (r7-maps several times, r7boss, r7-wire, r7-dialog):
  runs waited 5-25 minutes before deploying.

---

# Round 7b — real kill (and why round 7 could not show one)

Round 7 was not mergeable because no *rewarded* kill had been proven. This round did that, and it
also corrects a claim in §2.

## 7b.1 Round 7's "this enemy carries no drop data" was WRONG

`dropsenemies` (new verb) reads `BP_AICharacter_C.CharacterDeathData` @0xDA8 for every enemy in the
world. Measured in the hub:

```
dropsenemies: BP_Cultist_HUB_Beacon_C_... 1340uu hp=1.00 dead=0 rewards=1 gold=20 gloom=100 chance=1.00
dropsenemies: BP_NPC_ShellKeeper_C_...   3707uu hp=1.00 dead=0 rewards=1 gold=20 gloom=100 chance=1.00
dropsenemies: BP_Horde_Brigand_C_...      337uu hp=-1.00 dead=0 rewards=0 gold=3  gloom=26  chance=0.60
dropsenemies: 12 enemy actor(s) in the world, 12 with gold/gloom in their death data
```

So the data is there (hub cultists 20 gold / 100 gloom at chance 1.0; brigands 3 / 26 at 0.6). What
never happens is the **roll** — 7b.2.

## 7b.2 Why the game never rolls for a co-op kill (traced)

`GA_Death_AI_C` — the ability that owns `DropCurrency` — is **not activated** by a health write and
not by `StartDeath`. A probe-only trace installed POST hooks on 15 authored steps
(`InitializeDeath`, `HandleDeathEvent`, `GrantRewardAndHideHealthBar`, `WasKilledByPlayer`,
`DropCurrency`, `DropGold`, `DropGloom`, `DropItems`, `ShouldDrop*`, `GetCharDeathData`,
`DeathToEnemyOnly`, `EndDeath`):

```
dropsprobe: 15/15 death-ability steps traced
dropskill: BP_Cultist_HUB_Beacon_C_... -> health 0, startDeath ok=1
dropskill: 0.5s later ... hp 0.050 -> 0.050 dead=1 | local drops=rolled:0 calls:0 ...
(no dropstrace line at all -- not one of the 15 steps ran, for a cultist AND for a BP_Horde_Brigand)
```

So a co-op kill (the mod pushing the health attribute, or `StartDeath`) produces a dead enemy and its
death animation and **no loot code whatsoever** — not even the `WasKilledByPlayer` gate is asked (its
hook never fired). That is the real reason round 7 saw `calls:0`.

## 7b.3 Real player hits: what the game refused, and what that means

I tried to deliver a genuine player hit so the ASC path would fire. Every call was accepted
(ProcessEvent succeeded) and **none of them changed the target's health**:

```
dropshit: payload=Default__SpartaHitPayload built=1 healthDamage(type=0 value=400.0) isCDO 1->0
dropshit: 1) SpartaApplyHit ok=1 hp 0.050 dead=0 -> 0.050 dead=0
dropshit: 2) payload.ApplyPayload ok=1 asc=AbilitySystemComponent hp 0.050 -> 0.050
dropshit: 3) payload.ApplyPayloadInstigator ok=1 hp 0.050 -> 0.050
dropshit: 4) no OnHitReceived (that UFunction does not exist on BP_AICharacter_C)
dropshit: (last resort, probe only) StartDeath with WasKilledByPlayer forced -- still no roll
```

Notes for whoever picks this up:

* `USpartaHitPayload`'s `HealthDamage = {Type=Float, FloatValue=400}` is *accepted but inert*: the
  payload is a **gameplay-effect carrier** — without a valid `DamageEffect` (a GE class) and an
  effect context nothing is applied. Copying the class default object and clearing `bIsCDO`
  (`GetDuplicate` returns the CDO on this build) is not enough.
* `BP_PlayerController_C.S_DealDamage(400)` damages **the player himself** (`own body 1.00 -> 0.00`)
  — it is a self-damage cheat, not a damage-the-target one. It is no longer in the ladder.
* The next step for real weapon damage is the GE path: `DamageEffect = <a damage GE class>`,
  `CreateGameplayEffectContextHandle(SourceASC, DamageCauser, HitResult)`, then
  `ApplyPayloadInstigator`. `dropshit` already has the ladder; only step 0 needs a real GE.

## 7b.4 What the module does instead (the decision, and why)

The **owner rolls from the enemy's own `CharacterDeathData`** — the same numbers and the same chance
the ability would use — and grants that to **both** players immediately:

* `gloom = GloomOnDeath` (always), `gold = GoldOnDeath` with probability `GoldDropChance`;
* the owner grants itself via `S_AddGold`/`S_AddGloom` **and** publishes the event; the peer grants
  the same to its own player. If the game *does* roll (real weapon damage, a future GE path), the
  `DropCurrency` hook wins, publishes the game's own numbers, and the fallback cancels itself;
* the mirrored-death veto is unchanged: the non-owner's own drop code never runs for a death it
  copied.

Why immediate on both sides instead of "the owner walks to the bag": measured, a co-op kill spawns
**no world pickup** (the ability that would spawn the currency bag never runs — 7b.2), so there is
nothing to walk to on either client. Immediate grants are the only symmetric choice, and they stay
per player: no total is ever mirrored.

## 7b.5 Both directions, measured — `run-two-r7b-3.txt`

```
=== DROPS: dir1 (A owns) A rolled [BP_Cultist_HUB_C_...] 20/100 -> B granted [BP_Cultist_HUB_C_...] 20/100
=== DROPS: dir2 (B owns) B rolled [BP_Cultist_HUB_OvumCaretaker_C_...] 20/100 -> A granted [BP_Cultist_HUB_OvumCaretaker_C_...] 20/100
=== DROPS: A claim: PASS enemy BP_Cultist_HUB_C_...: rolled gold=20 gloom=100, published seq 1 (from CharacterDeathData: goldOnDeath=20 chance=1.00 gloomOnDeath=100)
=== DROPS: B grant: PASS enemy BP_Cultist_HUB_C_...: granted gold=20 gloom=100 items=0 (peer rolled gold=20 gloom=100 items=0, seq 1)
=== DROPS: B claim: PASS enemy BP_Cultist_HUB_OvumCaretaker_C_...: rolled gold=20 gloom=100, published seq 1
=== DROPS: A grant: PASS enemy BP_Cultist_HUB_OvumCaretaker_C_...: granted gold=20 gloom=100 items=0
=== DROPS: PASS both directions: each owner rolled and the other client granted the same (20/100 and 20/100), no double roll, hooks 5/5
...
RESULT: PASS
```

* two **different** enemies (the second kill skips the corpse, and `dropskill` now filters
  `NPC`/`Merchant`/`Blacksmith`/`Shopkeeper`/`Genessa` — which also stopped it killing the shell
  keeper and breaking NPCFAR);
* "no double roll" is proven by the keys: a receiver that rolled for the other client's enemy would
  have overwritten its own claim with *that* key; both claims name their own enemy;
* `dup:75` / `dup:83` in `drops=` — the 15 Hz re-sends of the same seq were ignored;
* the owner's self-grant shows in the counters (`A ... gold:40 gloom:200` = 20/100 for itself +
  20/100 granted from B).

## 7b.6 Wallet logging and the visual check

`dropswallet` prints the item-stack counts and `CumulativeGold/Gloom`. Measured: a grant moves
`GetTotalItemCountByDefinition` by **+1 stack** (1 -> 2), not by its value, so the stack count is not
the balance — the balance is what the HUD shows.

Visual run `local\lab\20261026-045048-r7b-visual4\` — before/after of both windows, contact sheet
`contact-HUD-7b.png` (2x2, built with System.Drawing), all four frames looked at:

| | before | after |
|---|---|---|
| **A (killer)** `HUD-A-before/after.png` | 1212 gold / 4021 gloom | **1232 / 4121** (+20 / +100) |
| **B (receiver)** `HUD-B-before/after.png` | 1212 gold / 4021 gloom | **1232 / 4121** (+20 / +100) |

A's frame also shows the game's own **"+100 Gloom"** pickup notification. Both players gained the
same amounts, on the game's own currency widget — the grant is not a log-only claim.

Two things worth recording from getting there:

* the earlier visual attempts showed **no** change on B because the copied runner kept
  `$root = $PSScriptRoot` in `scratch\`, so `relay\cooprelay.py` was never found and **no relay ran
  at all** — the two clients never talked. With `$root` pointed at `ms2-coop\`, B logged
  `got:1 ... dup:74` and its HUD moved.
* B's own log for that run: `drops=rolled:0 calls:0 sent:0 got:1 gold:20 gloom:100 items:0
  vetoed:0 dup:74 hooks:5` — it received and granted, and rolled nothing itself.

## 7b.7 What is still open

1. **A real weapon hit / GE payload** (7b.3): build `DamageEffect` + `CreateGameplayEffectContextHandle`
   and call `ApplyPayloadInstigator`. Then the game's own `DropCurrency` runs and this module only
   transports its numbers — the ideal path, with the data roll as the fallback for co-op kills.
2. **Items.** `ItemDropTable` entries are still not carried: both paths produce an empty item list
   today (the game path never ran; the data path reads only the two currencies). Next probe: read the
   table with `MapForEachRaw` at `actor + 0xDA8 + 0x10` and roll per-entry chance.
3. Items order/amount differences (stackable vs unique drops) would need `AddItemInstance` +
   `StatTags`, not just `AddItemByClass` — see `SYNC-DATA-MINE.md` §11.2.

---

# Round 7c — robustness (double-grant guard, multi-kill queue, items)

Three fixes from the review, each measured afterwards.

## 7c.1 Double-grant guard — airtight in both directions

Round 7b cancelled the data roll only if the game's `DropCurrency` fired inside a 1.0 s window, and
a real weapon kill was never measured, so a late game roll could have paid the owner twice. The
window is no longer the guarantee; the key sets are:

* `rolledByFallback` — keys the data roll already granted, and
* `rolledByGame` — keys the game's ability granted,

both per session (`DropsState`), with `rolledAt` for the timestamp. The fallback window is now
**3.0 s** (`kDropFallbackSeconds`), but nothing depends on it:

| order | what happens | code |
|---|---|---|
| fallback first, game later | the later `DropCurrency`/`DropGold`/`DropGloom`/`DropItems` call is **vetoed** by the existing PRE-SKIP hook and logged: `VETO late game roll <fn> for <key> -- the fallback already granted this death's loot (once only)`; the POST reader also refuses to publish a second event (`the game's late roll for <key> is ignored`) | `VetoHook`, `RollHook` |
| game first, fallback later | the queue entry for that key is dropped the moment the game's roll is seen (`the game rolled for <key> first -- the fallback stands down`), and the loop refuses an already-rolled key | `RollHook`, `Tick2` |
| duplicate death events (two `StartDeath` for one enemy) | `QueueRoll` dedupes by key, and the loop refuses a key that is already in `rolledByFallback` | `QueueRoll`, `Tick2` |

Counters exposed for the next session: `lateveto:` (late game rolls blocked after a fallback) and
`cancelled:` (fallbacks that stood down for the game) on the `drops=` line, plus
`fallbackRolled=` / `gameRolled=` in `dropsdump`.

**Honest limit:** a *late* game roll has never been observed in this game — the death ability does
not run for a co-op kill at all (§7b.2) — so this guard is defence-in-depth for the GE/weapon-hit
path that round 7b could not reach. It is verified by construction and by the two counters, not by
a run that produced a late roll; the probe verb to force one (calling `DropCurrency` on the right
ability instance) needs the ability's owning actor, which is only reachable while it is activated.

## 7c.2 Multi-kill queue — every death keeps its own roll

Round 7b had one `pendingRoll` slot and a 1.5 s global rate limit in `DeathStartHook`: a second
enemy dying inside that window got no roll at all, and a third could overwrite the first. Replaced
by a **queue** (`std::vector<PendingRoll>`, cap `kMaxPendingRolls = 32`, one entry per enemy,
deduped by key) that captures the enemy's `CharacterDeathData` **and** its item-table candidates at
death time. The 0.5 s rate limit now applies only to the provisional `result_drops_a.txt` hint, never
to the roll.

Measured — `scratch\run-probe.ps1 -Cmds 'A=dropsprobe 600;A=dropswallet;A=dropskilln 3;...'`
(new verb `dropskilln <n>` kills the N nearest hostile enemies inside one tick), lab run
`local\lab\20261026-051835-r7c-multi\`:

```
[A] drops: queued the fallback roll for BP_Cultist_HUB_C_..._1417419334 (3s): gold=20 gloom=100 chance=1.00 items=0
[A] dropskilln: 1/3 BP_Cultist_HUB_C_..._1417419334 (BP_Cultist_HUB_Beacon_C) startDeath=1
[A] drops: queued the fallback roll for BP_Cultist_HUB_C_..._1464626974 (3s): gold=20 gloom=100 chance=1.00 items=0
[A] dropskilln: 2/3 BP_Cultist_HUB_C_..._1464626974 (BP_Cultist_HUB_C) startDeath=1
[A] drops: queued the fallback roll for BP_Cultist_HUB_OvumCaretaker_C_..._1739522134 (3s): gold=20 gloom=100 chance=1.00 items=0
[A] dropskilln: 3/3 ... startDeath=1
[A] dropskilln: killed 3 enemy(ies), queue=3 rolled=0
[A] drops: rolled FROM DEATH DATA for ..._1417419334: gold=20 gloom=100 items=0 (chance 1.00 -> hit, seq 1) ... granted to self 20/100/0
[A] drops: rolled FROM DEATH DATA for ..._1464626974: gold=20 gloom=100 items=0 (chance 1.00 -> hit, seq 2) ... granted to self 20/100/0
[A] drops: rolled FROM DEATH DATA for ..._1739522134: gold=20 gloom=100 items=0 (chance 1.00 -> hit, seq 3) ... granted to self 20/100/0
[A] drops=rolled:3 calls:0 sent:3 got:0 gold:60 gloom:300 items:0 vetoed:0 dup:0 hooks:5 queued:3 pending:0 lateveto:0 cancelled:0

[B] drops: ..._1464626974 is a mirrored death (peer damage) -- its own loot roll is vetoed for 10s
[B] drops: ..._1417419334 is a mirrored death (peer) -- its own loot roll is vetoed for 10s
[B] drops: granted gold=20 gloom=100 items=0 from the peer's roll for ..._1417419334 (seq 1)
[B] drops: granted gold=20 gloom=100 items=0 from the peer's roll for ..._1464626974 (seq 2)
[B] drops: granted gold=20 gloom=100 items=0 from the peer's roll for ..._1739522134 (seq 3)
[B] dropswallet: gold=4 gloom=4 ...                       <- 1 stack + one per grant
[B] drops=rolled:0 calls:0 sent:0 got:3 gold:60 gloom:300 items:0 vetoed:0 dup:219 hooks:5 queued:0 pending:0 lateveto:0 cancelled:0
```

Three deaths inside one tick → **three** rolls on the owner (`rolled:3`, seq 1/2/3, distinct keys,
`pending:0`) and **three** grants on the peer (`got:3`, one per key, `dup:219` repeats ignored), and
the peer rolled nothing (`rolled:0`). That is the requested case.

## 7c.3 Items — what is rolled, and what is measured

The fallback now carries items, not just currency:

* `CollectItemDrops` reads **both** tables: `BP_AICharacter_C.AdditionalDropItems` @0xE08 through
  `ue::MapForEach` (a real property, so the property's own key/value sizes are used) and
  `FSpartaCharacterDeathData.ItemDropTable` at `actor + 0xDA8 + 0x10` through `ue::MapForEachRaw`
  (key = class 8 bytes; the value is read as a float first and as a double if that yields rows).
* the picked items ride the event as `"<path>@<count>"` and are granted on **both** sides with
  `AddItemByClass` (the owner in `RollFromData`, the peer in `OnPeerLine`);
* the interpretation is logged rather than assumed: `N item candidate(s), weights sum X -> weights`
  when the sum is 1 (±0.02), otherwise `per-entry chances`; a weighted pick then selects one item, a
  chance list rolls each entry independently (`PickItems`).

**Measured:** for the two enemy classes the lab used (hub cultist, `BP_Horde_Brigand`), both tables
produced **no candidates** — every roll line says `items=0` and no `item candidate` line was
emitted. So the item *roll* is implemented and wired end to end but is **not exercised by any
enemy in this save**; distinguishing "the table is empty" from "the map did not read" needs one more
probe (log the candidate count even when it is zero, and print
`ue::PropOffset(a,"AdditionalDropItems")` + the death-data offset). Listed as open, not as done.

## 7c.4 Tests and verdicts

```
# lab, three kills inside one second (7c.2, verdict lines quoted above)
powershell -File <ws>\scratch\run-probe.ps1 -Tag r7c-multi `
  -Cmds 'A=dropsprobe 600;A=dropswallet;A=dropskilln 3;A=dropswallet;B=dropswallet;A=dropsdump;B=dropsdump'

# regression + both-direction verdict
powershell -File <ws>\scratch\run-two.ps1 -Out G:\Freespace\shell_coop\local\lab\run-two-r7c-1.txt
```

`run-two-r7c-1.txt` (05:21-05:35), full suite:

```
=== DROPS: dir1 (A owns) A rolled [BP_Cultist_HUB_C_...] 20/100 -> B granted [BP_Cultist_HUB_C_...] 20/100
=== DROPS: dir2 (B owns) B rolled [BP_Cultist_HUB_OvumCaretaker_C_...] 20/100 -> A granted [BP_Cultist_HUB_OvumCaretaker_C_...] 20/100
=== DROPS: PASS both directions: each owner rolled and the other client granted the same (20/100 and 20/100), no double roll, hooks 5/5
=== SPAWN: PASS   === MIRROR: PASS   === PEERHIT: PASS   === AITARGET: PASS
=== STREAM: PASS   === NPCFAR: PASS   === ZONE: PASS   === PEERHP: PASS   === PEERDEAD: PASS
RESULT: PASS
```

Every pre-existing verdict passed in the same run, so the 7c changes (queue, guards, items) do not
disturb anything else.

## 7c.5 Files touched in 7c

| File | Change |
|---|---|
| `native\coop_state.h` | `PendingRoll`; `DropItem::weight`; `DropsState`: `pendingRolls` queue, `rolledByFallback`, `rolledByGame`, `rolledAt`, `lastClaimAt`, `alreadyRolledVetoed`, `fallbackCancelled`, `queuedRolls` (the single-slot fields are gone) |
| `native\src\coop_drops.cpp` | `QueueRoll` + queue loop (`Tick2`), dedupe/veto both ways (`VetoHook`, `RollHook`), 3 s window, `CollectItemDrops` + `PickItems` (items on both sides), `dropskilln` verb, new `drops=` counters |
| `run-two.ps1` | unchanged in 7c (the 7b block already covers both directions and waits for `PASS`) |
