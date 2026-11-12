# Mortal Shell II co-op — design

Status: research/prototype. Written 2026-10-22 against Mortal Shell II build
`++Sparta-Depot+Day1+CL92086-CL-0`, Unreal Engine 5.6, exe SHA-256
`BDEDDB33D61E876E94B6C30C8EA198AECFDEFFB557D6F5678C92D1C4C17AAE69`.

## What the game gives us

| Fact | Evidence |
|---|---|
| UE 5.6, shipping build, shipping paks in IoStore form | UE4SS log `Using engine version: 5.6`; `.utoc/.ucas` |
| Mod loader works: UE4SS with an official `MortalShell2` config | UE4SS log: PS scan ok, `StaticConstructObject_Internal <- Lua Script`, mods run |
| Game code is C++ module `Sparta` + Blueprints under `/Game/Sparta/` | 508 `/Script/Sparta` classes, `BP_*` classes |
| GAS-based combat/attributes | `EnemyAttributeSet`, `DarkPowerAttributeSet`, `AbilityTask_*`, `BTDecorator_*` |
| Persistent world state through save objects (`BPSO_*`) and `SpartaSaveSubSystem` | object dump |
| **No game netcode** | every replication symbol in the dump is UE boilerplate (Engine/Iris/OnlineSubsystemUtils) |
| Menu entry points callable from Lua | `WBP_MainMenu_C:ContinueGame / LoadGame / StartNewGame / LoadMap` |

## Shape of the mod

```
game.exe  +  UE4SS (dwmapi.dll proxy)
               └── Mods/CoopMod (Lua)          <- game-side logic, no compiler needed
                     │  file contract: state.txt in, peer.txt out, cmd.txt in
                     ▼
              cooprelay.py (Python 3, ctypes for Steamworks)
                     │  --transport file   (tests, one PC)
                     │  --transport udp    (LAN / two clients on one PC)
                     └  --transport steam  (AppID 480 relay, friends, no port forwarding)
```

Why a helper process: UE4SS's Lua sandbox has no sockets, and this machine has no C/C++
compiler (only python/node/dotnet). The relay is also where Steam lobbies/invites live, so
the Lua mod stays tiny and testable.

## Authority model

Host-authoritative state sync (playbook `how-coop-mods-work.md` §1.6). The host is the
player who created the session.

| Thing | Owner | Everyone else |
|---|---|---|
| Own player transform/animation | each player (own character) | renders the other's puppet |
| Enemies: AI, position, health, death | host | mirrors state, AI suppressed |
| Doors/gates/chests/quest flags (`BPSO_*`, `WorldStateTags`) | host | applies replicated writes, re-entrancy guarded |
| Saves | host's save is canonical | guests never write the host's world file |

Rule from `actor-ownership.md` applies once NPCs are mirrored: one owner per actor, with
epochs. Start without it (single authority = host), add epochs if/when NPC ownership
transfers to the nearest player.

## Message contract (v1, one JSON object per line)

```jsonc
{"t":"hello","v":1,"player":"A","build":"CL92086"}
{"t":"p","id":"A","x":1.2,"y":3.4,"z":5.6,"yaw":90.0,"pitch":0.0,"w":0.0,"anim":"idle","seq":17}
{"t":"bye","id":"A"}
```

- positions at 15 Hz, `seq` monotonic per sender for ordering/late-drop.
- `anim`: a cheap state string (`idle|walk|run|attack|hurt|dead`) derived on the sender; the
  receiver picks a loop, never a full graph.
- later: `{"t":"npc", ...}`, `{"t":"flag", ...}`, `{"t":"save", ...}`.

## Matching the peer's actors (discovered, not designed)

Both clients load the same save, and this game spawns the world deterministically: the
*same* actors get the *same* object names on both machines (e.g.
`BP_Cultist_HUB_C_UAID_D8BBC1D05326704902_..._1417419333`, and even the player pawn kept
`BP_PlayerCharacter_C_2147479936` on both clients). So NPC mirroring needs no id allocation
protocol: the host publishes the actor's name tail, the guest maps it to its own actor of
the same name. If a future build breaks that, fall back to (class, position) matching.

## Sync slices (each ends in a test)

1. **Puppet** — one client shows a body driven by the peer's position from a file.
   Exit test: puppet position equals the peer's commanded position within 5 units.
2. **Two clients, file transport**, same PC, two copies → both directions.
3. **Relay (UDP)** — same test over a socket, still one PC.
4. **Peer body: shell + animation** (detailed in "How the peer's body looks and moves"
   below) - the peer is rendered as *their* shell mesh and animated with the game's own
   locomotion sequences.
   Former entry, kept for history: animation state — walk/idle/run follows movement, checked by the mod's own log.
5. **Enemy/NPC sync** — DONE for transforms: the host publishes the 10 nearest Characters
   (excluding players) inside its player message at 15 Hz; the guest maps them by name and
   sets their location. Verified by a two-client test that moves an NPC ~400 m on the host
   and requires the guest's copy to follow (`20/20 within 30u` after the move).
   **Combat** - health is read through the game's own `SpartaHealthComponent`
   (`GetHealth / GetMaxHealth / GetCurrentHealthNormalized / IsDeadOrDying`) and death is
   triggered through `SpartaHealthComponent:StartDeath()`, so the native death animation
   plays instead of the body vanishing. Both clients publish the lowest normalized health
   they have seen for an enemy and adopt anything lower from the peer, which converges
   monotonically; a dead tail is broadcast and started on the other client.
   Still open: drops, quest flags, and the guest's local AI for enemies.
6. **World/quest flags** — replicate writes to `BPSO_*`/`WorldStateTags` from host,
   re-entrancy guarded; snapshot-on-join.
7. **Session flow** — host hosts from a save; guest follows into the same zone; disconnect
   handling; save stays host-side.
8. **Steam transport** — lobby bring-up done (init, logon, friends-only lobby, LobbyEnter,
   manual dispatch, using the game's own `steam_api64.dll`). The message leg between two
   accounts is untested: it needs a second Steam login on another machine.

## Constraints and risks

- The game is single-player by design; both players run their own copy of the same zone.
  Anything the host's world does that the guest's copy can't reproduce (scripted set pieces,
  boss state) needs explicit sync — track as known gaps rather than pretending.
- Saves are shared per Windows user (`%LOCALAPPDATA%\MortalShell2\Saved\SaveGames`). Two
  copies on one PC share them, so the harness snapshots/restores around runs.
- UE5.6 + experimental UE4SS: `SpawnActor` from Lua may be unstable (upstream #527 in the
  older Poppy case). The proven fallback is the game's own `summon` console command, then
  adopt the actor by name diff.
- Laptop must stay usable during tests: windowed 1024x576, `-nosound`, one run at a time,
  background job, saves restored afterwards.

## Non-goals (for now)

- A real Unreal listen-server co-op (`?listen`) — the game's GameMode is not written for a
  second local/remote player, and there is no netcode to piggyback on.
- Anti-cheat or online-service interaction: none exists in this game.

## How the peer's body looks and moves (round 2)

`summon /Game/Sparta/Core/Player/BP_PlayerCharacter.BP_PlayerCharacter_C` gives a body
with a `SpartaSkeletalMeshComponent` that has an AnimBP (`ABP_Player_C`) but **no mesh
asset**, so it is invisible. Three lab findings drive the implementation:

| Finding | Evidence |
|---|---|
| The mesh asset can be swapped only after its AnimBP instance is gone; swapping while it is alive crashes inside the mesh/anim re-init | crash dumps `crash_2026_10_22_02_38_16` and `02_44_27`; `SetAnimInstanceClass(nil)` + `SetSkeletalMeshAsset(shell)` survived and reached `bones=164` |
| A controller-less pawn ignores `AddMovementInput` while `MovementMode == MOVE_None`, and putting it into `MOVE_Walking` and feeding input crashes the game inside the movement tick | lab runs: `travelled 0 uu` at mode 0; three crashes at the exact moment the drive started at mode 1 |
| The game's shared shell locomotion sequences load and play on that component | `PlayAnimation(A_Shared_Idle_H / A_Shared_Locomotion_Walk_Fwd_H2 / A_Shared_Locomotion_Sprint_800_H2, true)` -> `playing=true` |

So the peer body is built like this:

1. each client publishes the **path of its own current shell mesh** (`body` in the state
   line, e.g. `/Game/Sparta/Characters/Shells/DarkForm_Bulk/Art/Mesh/SK_DarkForm_Bulk.SK_DarkForm_Bulk`)
   plus its AnimBP path (`apart`);
2. the receiver clears the puppet's AnimBP, assigns that mesh and keeps the body visible -
   which also handles **shell swaps**: when the peer changes shell the path changes and
   the mesh is swapped again;
3. locomotion uses the game's own shell sequences in single-node mode, chosen from the
   puppet's actual speed (`idle < 45 < walk < 420 < sprint`) with the play rate scaled to
   that speed; no AnimBP of ours is involved, so no game-side state is required;
4. the body moves because the whole puppet actor is moved to the peer's reported
   transform, smoothed (~40 % per 30 Hz tick, snap beyond 400 uu). Because of the crash
   above we deliberately do **not** drive `CharacterMovement` on the puppet.

Known limits: feet slide rather than step exactly at the peer's speed, no per-shell
upper-body/weapon animation, and the peer's shell asset must exist in our install (same
game build, so it does).

## Combat sync: keeping two independent worlds converged

Neither client can be authoritative in the netcode sense, so combat uses a **monotonic**
rule:

- both clients sample the normalized health of nearby enemies twice a second and publish
  the lowest value they have seen for each enemy (`nhp`);
- on receipt a client adopts the peer's value only when it is **lower** than its own
  (`setHealthNorm` writes `SpartaHealthSet.Health` through `UProperty::ImportText`), so
  damage from either player lands in both worlds and the values can only fall;
- a client that sees an enemy die (health 0 or `IsDeadOrDying`) records the tail and
  publishes it in `dead`; the peer calls `StartDeath()` on its own copy once;
- a respawn (health back at ~1 while we had it dead) clears the dead flag and is adopted
  too, so one wipe does not leave enemies permanently dead on one side.
