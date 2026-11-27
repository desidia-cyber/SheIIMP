# ShellMP — Mortal Shell II Co-op Mod (v1.0)

Native drop-in **2-player co-op mod** (`dxgi.dll`) for **Mortal Shell II** (UE 5.6, Day1 and Nov 2026 builds).
**No UE4SS, no Python, no external relay, no injector.** Just drop `dxgi.dll` next to `MortalShell2-Win64-Shipping.exe`, launch the game normally, and press **F9** to open the in-game CO-OP menu.

## Requirements

| | |
|---|---|
| Game | Mortal Shell II (PC, Day1 or Nov 2026 build) |
| OS | Windows 10 / 11 x64 |
| Network | Steam P2P (built-in, Game AppID `2584270` or Spacewar `480`) **or** direct LAN UDP |

## Install (each player, once)

**Option A — Installer script (PowerShell):**
```powershell
.\Install.ps1 -GameDir "E:\Games\Mortal Shell II"
```
*(Add `-Force` if updating an existing `dxgi.dll`.)*

**Option B — Manual drop-in:**
Copy `files\dxgi.dll` into:
```text
<GameDir>\MortalShell2\Binaries\Win64\dxgi.dll
```
> **Note:** Do not run UE4SS (`dwmapi.dll` / `UE4SS.dll`) alongside `dxgi.dll` — the two hook layers will conflict. `Install.ps1` disables UE4SS automatically if present.

## How to Play (In-Game F9 Menu)

1. Launch **Mortal Shell II** normally on both PCs and load your save.
2. Press **F9** to open the CO-OP overlay (navigate with **Arrow keys + Enter**, **Escape** to close).
3. **Steam P2P (recommended):**
   - Make sure Steam is running and logged in on both PCs.
   - Leave **Network** on `STEAM P2P`.
   - Choose **Steam Identity**:
     - `MORTAL SHELL II` (`2584270`) if both players own the game on Steam.
     - `SPACEWAR (480)` + select `[ RESTART AS SPACEWAR (480) ]` otherwise.
   - Enter the **exact same Co-op Password** on both PCs.
   - Host selects `[ START HOST ]`; partner selects `[ JOIN SESSION ]`.
   - *(Automatic Steamworks detection: uses `steam_api64.rne` if present in the game's Steamworks `Win64` folder, otherwise uses `steam_api64.dll`.)*
4. **LAN UDP (offline / direct IP / VPN like Radmin or ZeroTier):**
   - Switch **Network** to `LAN UDP`.
   - Set **LAN Server IP** (host's IP) and **LAN Port** (default `27015`).
   - Host selects `[ START HOST ]`; partner selects `[ JOIN SESSION ]`.

Settings persist automatically in `MS2Coop-Net.ini` next to `dxgi.dll`.

## Features (v1.0)

- **In-DLL Steam P2P & LAN UDP + F9 HUD Menu:** Password-keyed Steam lobbies (20,000-round SHA-256) and direct LAN UDP fallback; DX12-safe UE5 HUD rendering.
- **Full Player Puppet & Appearance Sync:** Position, velocity, facing, locomotion, montages, Shell mesh swaps, weapons, masks, clothing, skins, traversal wall-squeeze animations, query-only capsule collision, footstep foley, active Seal & Harden stone-form, animal/object transformations, and player clones / ally & Shell summons.
- **HitHook Combat Sync:** Native `USpartaHitPayload::ApplyPayloadInstigator` detour syncing real damage, poise/break, hit reactions, guest parries (`Event.Hit.Parried`), flyback, and `DodgeVeto` on phantom enemy->puppet hits.
- **Projectiles, Grabs & VFX:** Replicates `ASpartaProjectile` shots, `TriggerPairedAnimation` ripostes/grabs, and attached Niagara VFX.
- **Bosses, Beacons & Difficulty Scaling:** 34 bosses & minibosses (including multi-phase bosses and Zmey), fog-wall partner pull & HeadTree underground/surface handling, beacon cleanse sync + host fast-travel follow, cutscene lock + mutual skip-voting, and automatic 150% enemy HP scaling in co-op (restored to 100% solo).
- **Shared World, Quests & Dungeons:** Doors, levers, lifts, world elevators, bridges, stone gates, pressure-plate doors, illusory walls, bear traps, chests, breakables, shared enemy drops, Vlas cage, Ova, 13-NPC questlines & dialogue vote sync, host-authoritative TombZone dungeon variants, Thestus Night Mode, non-proximity horde spawners, and NG+ safety guard during active co-op.
- **Co-op Death Rules:** Solo death respawns the player with a debuff stack without resetting the world; boss fights hold until party wipe or victory; party wipe resets cleanly.

## Uninstall

Delete `dxgi.dll` (and optionally `MS2Coop-Net.ini` / `ms2coop-native.log`) from `<GameDir>\MortalShell2\Binaries\Win64\`. If `Install.ps1` disabled UE4SS, move files back from `_ue4ss_disabled\`.
