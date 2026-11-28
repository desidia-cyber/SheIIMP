# ShellMP — Mortal Shell II Co-op Mod

A native, drop-in **2-player co-op mod** (`dxgi.dll`) for **Mortal Shell II** (Unreal Engine 5.6).

**No UE4SS, no Python runtime, no external relay, and no injector.** Drop `dxgi.dll` next to `MortalShell2-Win64-Shipping.exe`, launch the game normally, and press **F9** to open the in-game co-op menu.

---

## Current State (R10 / v1.1)

ShellMP has migrated completely from its earlier UE4SS + Lua + Python relay prototype to a **single self-contained native C++20 proxy DLL** (`ms2-coop/native`) with its own UE 5.6 reflection layer, MinHook detours, in-DLL Steam P2P / LAN UDP networking, and DX12-safe HUD overlay.

Active development is on branch `ms2-coop-r10`.

### What's Working & Verified

All core co-op systems have been implemented and verified on a two-client live test rig (`ms2-coop/rig.ps1`, Host + Guest):

- **Networking & In-Game UI**
  - Built-in **Steam P2P** (password-keyed lobbies using 20,000-round SHA-256, supporting Mortal Shell II AppID `2584270` or Spacewar `480` with one-click restart) and direct **LAN UDP**.
  - Native **F9** co-op menu and status toast rendered via `AHUD::PostRender` / `ReceiveDrawHUD` (DX12-safe, zero swapchain hooks), styled after the game's UI. Settings persist in `MS2Coop-Net.ini`.
- **Player Puppet & Appearance Sync**
  - Position, velocity, facing, locomotion, animation montages, Shell mesh swaps, weapons, masks, clothing, skins, traversal wall-squeeze animations, query-only capsule collision, and footstep foley.
  - Active **Seal** and **Harden** stone-form state (including proper unfreeze on release).
  - **Animal/object transformations** and **player clones / ally & Shell summons** (including full montage and playback-position sync).
- **Combat, Projectiles & VFX**
  - Native `USpartaHitPayload::ApplyPayloadInstigator` detour syncing real damage, poise/stagger breaks, hitstun/reactions, guest parries (`Event.Hit.Parried`), flyback, and `DodgeVeto` on phantom enemy-to-puppet hits.
  - Ranged projectiles (`ASpartaProjectile`), paired animations (`TriggerPairedAnimation` grabs/ripostes), and attached Niagara VFX.
  - Automatic **150% enemy HP scaling** while connected in co-op (restored to 100% solo).
- **Bosses, Beacons & Death Rules**
  - **34 bosses & minibosses** tested live: 11/13 main story bosses (including Zmey `BP_FinalBoss_C` and 5 three-phase bosses) verified live across Phase 2 & Phase 3 sync; remaining 2 main bosses and all 21 minibosses verified for spawn, `BPC_BossAI` / `HealthComponent` compatibility, and shared drops.
  - Fog-wall partner pull on boss aggro, HeadTree underground/surface handling, beacon cleanse sync + host fast-travel follow, cutscene lock + mutual skip voting.
  - Co-op death policy: a solo death respawns the player with a debuff stack without resetting the world; boss encounters hold until party wipe or victory; party wipe resets cleanly.
- **World, Quests & Dungeons**
  - Doors, levers, lifts, world elevators, drawbridges/bridges, stone gates, pressure-plate doors, illusory walls, bear traps, chests, breakables, shared enemy drops, non-proximity horde spawners, Vlas cage, and Ova.
  - **6/6** `DT_QuestData1` quests, **13-NPC** questlines, dialogue vote sync, and **107/107** data layers synced cleanly.
  - Host-authoritative **TombZone** dungeon variant sync (`BP_LevelInstanceSelector_C`), **Thestus Night Mode** sync, and an **NG+ reset safety guard** (`S_ResetForNewGamePlus` veto during active co-op).

### Current Scope & Limitations

- **Player count:** 2-player co-op (Host + 1 Guest).
- **Supported builds:** Mortal Shell II PC (Windows 10/11 x64), UE 5.6 Day1 and Nov 2026 builds. Future game updates may require refreshing AOB signatures or fallback RVAs in `ms2-coop/native/src/ms2_ue.cpp`.
- **UE4SS incompatibility:** Do not run UE4SS (`dwmapi.dll` / `UE4SS.dll`) alongside `dxgi.dll` in the same game installation — the two hook layers will conflict. (`Install.ps1` handles disabling UE4SS automatically if present.)

---

## Installation & Quick Start

### Requirements
- **Game:** Mortal Shell II (PC, Day1 or Nov 2026 build)
- **OS:** Windows 10 / 11 x64
- **Network:** Steam P2P (built-in) or direct LAN UDP

### Install (each player)
1. **Installer script:**
   ```powershell
   .\ms2-coop\package\Install.ps1 -GameDir "E:\Games\Mortal Shell II"
   ```
2. **Or manual drop-in:** Copy `dxgi.dll` into:
   ```text
   <GameDir>\MortalShell2\Binaries\Win64\dxgi.dll
   ```

### How to Play
1. Launch **Mortal Shell II** normally on both PCs and load your save.
2. Press **F9** to open the co-op overlay (Arrow keys + Enter to navigate, Escape to close).
3. **Steam P2P (recommended):**
   - Keep **Network** on `STEAM P2P`.
   - Pick **Steam Identity** (`MORTAL SHELL II` if both own it on Steam, or `SPACEWAR (480)` + `[ RESTART AS SPACEWAR (480) ]`).
   - Enter the same **Co-op Password** on both PCs.
   - Host selects `[ START HOST ]`; Guest selects `[ JOIN SESSION ]`.
4. **LAN UDP (direct IP / offline):**
   - Switch **Network** to `LAN UDP`, set **LAN Server IP** (host IP) and **LAN Port** (default `27015`).
   - Host selects `[ START HOST ]`; Guest selects `[ JOIN SESSION ]`.

### Uninstall
Delete `dxgi.dll` (and optionally `MS2Coop-Net.ini` / `ms2coop-native.log`) from `<GameDir>\MortalShell2\Binaries\Win64\`.

---

## Building from Source

Requires **Visual Studio 2022** with the C++ x64 build tools (`cl.exe`) and Python 3 (for export-table verification).

```bat
cd ms2-coop\native
build.bat            :: builds ms2-coop\native\build\dxgi.dll (warning-free at /W3)
build.bat test       :: builds DLL + runs API contract, death policy, and AOB self-checks
```

To assemble a standalone distributable zip (`dxgi.dll` + `Install.ps1` + user `README.md`):

```powershell
.\ms2-coop\package\build-package.ps1
```

---

## Testing

- **Offline / build-time checks (`build.bat test`):**
  - `test_api.exe` — verifies UE 5.6 struct sizes and internal co-op module API contracts.
  - `test_coop_policy.exe` — verifies the co-op death policy truth table.
  - `test_scan.exe` — runs the DLL's AOB scanner against `MortalShell2-Win64-Shipping.exe` (set `MS2_EXE` to override the path) to confirm every pattern resolves uniquely.
  - `test_load.exe` — verifies `dxgi.dll` loads cleanly and stays dormant in a non-UE host process.
- **Two-client live rig (`ms2-coop/rig.ps1`):**
  - Boots Host + Guest once over LAN UDP, snapshots/restores saves automatically, and runs multi-suite gameplay checks (`atlas`, `shells_look`, `dlg_vote`, `world_interact`, `combat_fx`, `dungeon`, `harden_ngp`, `quests`, `boss_tour`).

---

## Repository Layout

```text
ms2-coop/
  native/              Native C++20 proxy DLL (dxgi.dll), UE 5.6 reflection core, MinHook, co-op modules & build.bat
    src/               proxy.cpp, ms2_ue.cpp, coop_*.cpp (net, steam, menu, puppet, combat, hithook, boss, world, fx_grabs, ...)
    tests/             API contract, death policy, AOB scan, and non-UE load tests
    thirdparty/        Vendored MinHook
  package/             Standalone release packager (build-package.ps1), Install.ps1, user README.md
  rig.ps1              Persistent two-client live co-op test harness
  docs/                Design notes, gap analyses, and milestone reports
  mod/ & relay/        Legacy UE4SS Lua + Python relay prototype (superseded by native/)
```
