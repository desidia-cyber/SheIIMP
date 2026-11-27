# Mortal Shell II Co-op (v1.0)

Native drop-in co-op mod (`dxgi.dll`) for **Mortal Shell II** (UE 5.6, Day1 and Nov 2026 builds).
**No UE4SS, no Python, no external relay, no injector.** Just drop `dxgi.dll` next to `MortalShell2-Win64-Shipping.exe`, launch the game normally, and press **F9** to open the in-game CO-OP menu.

## Requirements

| | |
|---|---|
| Game | Mortal Shell II (PC, Day1 or Nov 2026 build) |
| OS | Windows 10 / 11 x64 |
| Network | Steam P2P (built-in, Game AppID `2584270` or Spacewar `480`) **or** direct LAN UDP |

## Install (each player, once)

**Option A — Installer script:**
```powershell
.\Install.ps1 -GameDir "E:\Games\Mortal Shell II"
```

**Option B — Manual drop-in:**
Copy `files\dxgi.dll` into `<GameDir>\MortalShell2\Binaries\Win64\`.

## How to Play (In-Game F9 Menu)

1. Launch **Mortal Shell II** normally on both PCs and load your save.
2. Press **F9** to open the CO-OP overlay (navigate with Arrow keys + Enter, Escape to close).
3. **Steam P2P (recommended):**
   - Leave **Network** on `STEAM P2P`.
   - Choose **Steam Identity** (`MORTAL SHELL II` if both own on Steam, or `SPACEWAR (480)` + `[ RESTART AS SPACEWAR (480) ]` if needed).
   - Enter the same **Co-op Password** on both PCs.
   - Host selects `[ START HOST ]`; partner selects `[ JOIN SESSION ]`.
4. **LAN UDP (offline / direct IP):**
   - Switch **Network** to `LAN UDP`.
   - Set **LAN Server IP** (host's IP) and **LAN Port** (default `27015`).
   - Host selects `[ START HOST ]`; partner selects `[ JOIN SESSION ]`.

Settings persist automatically in `MS2Coop-Net.ini` next to `dxgi.dll`.

## Features (v1.0)

- **In-DLL Steam P2P & LAN UDP + F9 HUD Menu:** Password-keyed Steam lobbies (20,000-round SHA-256) and direct LAN UDP fallback; DX12-safe UE5 HUD rendering.
- **Full Player Puppet Sync:** Position, velocity, facing, locomotion, montages, Shell mesh swaps, weapons, masks, clothing, skins, traversal wall-squeeze animations, query-only capsule collision, and footstep foley.
- **HitHook Combat Sync:** Native `USpartaHitPayload::ApplyPayloadInstigator` detour syncing real damage, poise/break, hit reactions, guest parries (`Event.Hit.Parried`), flyback, and `DodgeVeto` on phantom enemy->puppet hits.
- **Projectiles, Grabs & VFX:** Replicates `ASpartaProjectile` shots, `TriggerPairedAnimation` ripostes/grabs, and attached Niagara VFX.
- **Bosses, Beacons & Difficulty Scaling:** Boss phase sync, fog-wall partner pull & HeadTree underground/surface quirk, beacon cleanse sync + host fast-travel follow, cutscene lock + mutual skip-voting, and automatic 150% enemy HP scaling in co-op (restored to 100% solo).
- **Shared World, Quests & Dungeons:** Doors, levers, lifts, chests, breakables, shared enemy drops, multi-stage quest & dialogue vote sync, host-authoritative TombZone dungeon variant sync (`BP_LevelInstanceSelector_C`), non-proximity horde spawners, pressure-plate doors, active Seal + Harden stone-form state, and NG+ (`S_ResetForNewGamePlus`) safety warning.
- **Co-op Death Rules:** DollsMP-style death policy (solo death respawns with debuff stack without resetting the world; boss fight holds until wipe or victory; party wipe resets cleanly).

## Uninstall

Delete `dxgi.dll` (and `MS2Coop-Net.ini` / `ms2coop-native.log`) from `<GameDir>\MortalShell2\Binaries\Win64\`. If `Install.ps1` disabled UE4SS, move files back from `_ue4ss_disabled\`.
