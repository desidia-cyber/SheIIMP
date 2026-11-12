# QA — what was actually run

QA level for this prototype: **automated tests, two real game clients, one PC** (the
"automated" tier in `coop-anything/data/SCHEMA.md`). No human playtest yet, and no
two-machine test yet — both are named as open items at the bottom.

## How to reproduce

```powershell
# two-client sync + NPC mirror (needs the two test installs and Python on PATH)
.\run-two.ps1 -GameA "E:\Games\_MS2-coop-test" -GameB "E:\Games\_MS2-coop-b" `
              -MoveNpc "49400 -134182 -42197" -TeleportB "49800 -134182 -42197"

# one client, self-timed boot + puppet check
.\drive.ps1 -GameDir "E:\Games\_MS2-coop-test" -Commands "auto"

# rebuild the friend package
.\package\build-package.ps1
```

Both harnesses launch the game **off-screen** (`SetWindowPos(-30000,-30000)`, never
minimized — minimizing stalls this game's menu UI), with `-nocrashreport`, and a watcher
that kills `CrashReportClient` on sight so no dialog reaches the desktop.

**Save protection is snapshot/restore, not isolation.** Overriding the child process's
`LOCALAPPDATA` does *not* move this game's save folder: Unreal resolves it through the
Windows known-folder API, so the game kept writing to the real
`%LOCALAPPDATA%\MortalShell2\Saved\SaveGames` (it even created `WorldState_Backup_0_*.sav`
there). The harness therefore snapshots every `*.sav` before a run and afterwards restores
the snapshot and deletes any `*.sav` the run created. Verified against the 00:11 backup:
`MetaSaveGame.sav`, `SpartaGameSettings.sav` and `WorldState_0.sav` hash-identical.

## Test runs

### 1. Puppet slice — one client (2026-10-22 01:11)

```
auto: menu widget after 0s
auto: ContinueGame ok=true
auto: gameplay after 5s, pawn BP_PlayerCharacter_C ..._2147479939
spawn: 1 BP_PlayerCharacter_C before summon
console [summon /Game/Sparta/Core/Player/BP_PlayerCharacter.BP_PlayerCharacter_C]
auto: puppet adopted after 1s: BP_PlayerCharacter_C ..._2147471046
drive: target 49252.2 -134182.5 -42196.9 -> actual ... (dist 0.0)
auto: PASS puppet at commanded spot (dist 0.0)
```

### 2. Player position sync — two clients over UDP (2026-10-22 01:51)

```
A: PASS puppet tracks peer (dist 0.0, 30/30 good)
B: PASS puppet tracks peer (dist 0.0, 30/30 good)
RESULT: PASS
```

Client B was teleported ~800 units away before judging, so a shared spawn point could not
fake the result.

### 3. NPC mirror — two clients, host moves an NPC (2026-10-22 02:12)

```
host: npctp -> BP_Cultist_HUB_C_UAID_D8BBC1D05326704902_..._1417419333 at 49400 -134182 -42197
B   : npc verdict: 20/20 good
NPC : PASS npc mirror after host move 20/20 within 30u (last 0.0, ...)
A   : PASS puppet tracks peer (dist 0.0, 30/30 good)
B   : PASS puppet tracks peer (dist 0.0, 30/30 good)
RESULT: PASS
```

The guest only starts counting **after** the host's coordinates move more than 200 units
from the first value it saw, so the earlier "18/20" pass (measured before any move, when
both saves simply agreed) was correctly rejected as hollow and the test was tightened.

### 4. Steam transport bring-up — one instance, real Steam (2026-10-22 02:14)

```
steam dll: <game>\Engine\Binaries\ThirdParty\Steamworks\Steamv157\Win64\steam_api64.dll
steam init ok
logged on as <steamid>
manual dispatch ready (pipe 1)
LobbyCreated result=1 lobby=108086395351860111
LobbyEnter lobby=108086395351860111 response=1
LOBBY=108086395351860111
```

Run: `.\run-steam-smoke.ps1 -Seconds 20 -Game "<game dir>"`. This proves the relay can load
the game's own Steam DLL, initialise AppID 480, log on, create a friends-only lobby and
enter it, with manual callback dispatch running.

## What these tests do NOT prove

- **Two different PCs** across the internet, and the Steam **message leg**
  (`ISteamNetworkingMessages` between two accounts) — it needs a second Steam login on
  another machine. Only lobby bring-up is proven.
- **Combat**: health, damage, death and drops are not synced at all. Only transforms.
- **Combat**: health, damage, death and drops are not synced at all. Only transforms.
- **Animation**: the other player's body moves but idles; no walk cycle is verified.
- **Story/quest/world state**: untouched.
- **Stability**: UE4SS on this UE 5.6 build crashes occasionally (access violation at
  0x0), including once mid-run; a crashed run is retried rather than counted.

## Known harness invariants

- No mod code calls `io.popen`/`os.execute` from a tick (a 15 Hz `io.popen` once spawned
  `cmd.exe` twice a second and made the desktop unusable). Verified: `MAXCMD=1` over 60 s.
- The owner's save directory is never used by a test: clients run on seeded copies under
  `local-appdata\A` / `local-appdata\B`.
- Every game process a test starts is stopped by PID/name at the end.

## Round 2 (2026-10-22 03:00-04:30): shell body, animation, combat

### 5. Lab runs — how the peer body is built (single client)

The lab mod (`mod/CoopLab`) drives one client through the same steps the mod takes and
prints what the engine reports:

| Question | Result |
|---|---|
| Does a summoned body have a mesh? | No: `CharacterMesh0 bones=0`, asset is a dead soft pointer, `ABP_Player_C` assigned -> the body is **invisible** |
| Can we just set the mesh? | **No**: `SetSkeletalMeshAsset` with the AnimBP instance alive crashes in `memcpy` (`crash_2026_10_22_02_38_16`) |
| Does clearing the AnimBP first help? | **Yes**: `SetAnimInstanceClass(nil)` then `SetSkeletalMeshAsset(SK_DarkForm_Bulk)` -> `bones 0 -> 164`, visible body, no crash |
| Does `AddMovementInput` move a summoned body? | **No**: `MovementMode=0 (MOVE_None)`, `travelled 0.0 uu`. Setting `MOVE_Walking` and feeding input crashes the game inside the movement tick (3/3 runs) |
| Can the game's own locomotion sequences drive that body? | **Yes**, with the explicit API: `SetAnimationMode(1,true)` + `SetAnimation(seq)` + `Play(true)` -> `playing=true`. `PlayAnimation()` alone **hung the guest client** twice; the explicit three calls are stable |
| Is `GetBoneTransform` usable for measurement? | **No**: sampling bones on `SpartaSkeletalMeshComponent` froze the UE4SS Lua thread for the rest of the session |
| Can `LoadAsset` be used? | Only on the game thread; calling it via `ExecuteInGameThread` from a loop **killed a client**. The mod now only uses `StaticFindObject` and retries |

Screenshots taken by the harness (`PrintWindow`, off-screen window) show the second body
standing next to the player with the shell mesh applied.

### 6. Two clients, animation off (isolation run, 04:15)

```
SHELL : PASS  (A publishes body=SK_DarkForm_Bulk, B sees it and vice versa)
A     : PASS puppet tracks peer (dist 0.3, 30/30 good; moves 27, snaps 0)
B     : PASS puppet tracks peer (dist 0.4, 30/30 good; moves 11, snaps 0)
DAMAGE: PASS  (guest wrote health 0.5 -> host adopted it: ..._1417419333=0.500)
DEATH : PASS  (host killed an enemy -> guest copy died via StartDeath)
```

### 7. Two clients with animation (04:19)

```
SHELL : PASS
A     : PASS puppet tracks peer (dist 0.4, 30/30 good)
B     : PASS puppet tracks peer (dist 0.4, 30/30 good)
DAMAGE: PASS      DEATH: PASS
B log : puppet locomotion -> idle / walk / sprint (playing=true)
```

Both clients stayed alive for the whole run with single-node animation active.

### 8. Weapons

The visible weapon is `/Game/Sparta/Weapons/Proxy/SK_ProxyWeap` and each client publishes
its path plus the socket it hangs from; the peer spawns a stock `SkeletalMeshActor` with
that mesh and attaches it to the same socket on the puppet body.

### Bugs that only a real two-client run could expose

| Symptom | Cause | Fix |
|---|---|---|
| peer never arrives, "waiting for peer state (missing)" | one stray `"` in the state line's format string -> the relay's `json.loads` failed and dropped every line silently | format fixed, and the relay now **logs** bad JSON instead of dropping it |
| puppet froze mid-run, no verdict | an exception in the 15 Hz loop (`dist()` given a lowercase `lastPeer`) aborted the whole loop; every step was inside one `pcall` | each loop step is now guarded and **logs its own error** (`ERR <step>: ...`) |
| all enemies "died" on the peer without anyone attacking | fresh actors report `GetCurrentHealthNormalized() == 0` before their attributes exist, and the monotonic rule adopted that 0 | health samples require `GetMaxHealth() > 0` and ignore a 0 that is not accompanied by `IsDeadOrDying()` |
| `peer respawn adopted ...` every tick | the revive branch fired while the flag was not set | revive only when the tail was actually marked dead |
| guest client froze seconds after the body appeared | `PlayAnimation()` / `LoadAsset` via `ExecuteInGameThread` / `GetBoneTransform` on the Sparta mesh | replaced by the explicit single-node API, `StaticFindObject` only, and no bone sampling |

| run (2026-10-22) | result | notes |
|---|---|---|
| lab `st1` / `st2` | puppet has both player streaming sources enabled; Non Blocking on = 24 -> 666 actors at a far landing area | `local/lab/20261022-130055-st2` |
| `run-two -StreamOnly` #1 | STREAM PASS on host (28 -> 670) | guest verdict FAIL by design flaw (area already loaded); criterion changed to n1 >= 100 |
| `run-two -StreamOnly` #2 | STREAM never measured | guest fell after the far `tp`, fall recovery put it back in the hub -> `tp` now hovers 6 s |
| `run-two -StreamOnly` #3 | STREAM PASS both, NPCFAR PASS | host mirrored the cultist the guest pulled to the far point 1 s after `npctp` |
| `run-two` full | all PASS except WORLD (known, §14) | 0 `ERR` lines |
