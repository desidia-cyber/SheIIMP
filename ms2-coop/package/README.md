# Mortal Shell II Co-op (prototype 0.2)

Two players, each running their own Mortal Shell II, share a session: you see the other
player as their **actual shell** (mesh + walk/run animation), your positions stay in sync,
nearby enemies are mirrored and their **health and death are synced**. Saves, quest flags
and drops are **not** synced - see [What works](#what-works).

The mod is a native proxy DLL (`dxgi.dll`): **no UE4SS, no injector, no launcher flags.**
Windows loads it with the game because the shipping exe already imports that name.

## Requirements

| | |
|---|---|
| Game | Mortal Shell II, PC build `++Sparta-Depot+Day1+CL92086-CL-0` (Unreal Engine 5.6) |
| OS | Windows 10/11 x64 |
| Python | 3.8+ ([python.org](https://www.python.org/downloads/), tick "Add to PATH") |
| Steam | Running and logged in on both PCs (the relay uses AppID 480, "Spacewar"; nobody needs to own it) |
| Both players | the same game build; different install paths are fine |

UE4SS is **not** needed. If you already have it installed for other mods, `Install.ps1`
moves it into `_ue4ss_disabled\` instead of letting the two run together (see
[Uninstall](#uninstall)); nothing is deleted.

## Install (each player, once)

1. Unzip this package anywhere.
2. Open PowerShell in that folder and run:
   ```powershell
   .\Install.ps1 -GameDir "E:\Games\Mortal Shell II"
   ```
   (use your own game folder). The installer:
   * copies `dxgi.dll` next to `MortalShell2-Win64-Shipping.exe` (an older, different
     `dxgi.dll` there is kept once as `dxgi.dll.rollback`);
   * moves `dwmapi.dll`, `UE4SS.dll`, `UE4SS-settings.ini`, `UE4SS.log`, `ue4ss\`,
     `UE4SS_Signatures\`, `UE4SS_SDK_Backends\` and `Mods\` into
     `<game>\MortalShell2\Binaries\Win64\_ue4ss_disabled\`;
   * writes `coop_dir.txt` and creates the `coop\` state dir with the relay in it.

   It warns if Python or Steam is missing. Use `-Force` to replace an already installed
   `dxgi.dll` with this package's build.

## Uninstall

From `<game>\MortalShell2\Binaries\Win64\`:

* delete `dxgi.dll` (and `dxgi.dll.rollback`, if it exists - or rename it back to
  `dxgi.dll` if you want the file the installer replaced);
* move whatever you want to keep out of `_ue4ss_disabled\` back into this folder
  (that restores the previous UE4SS install); delete the folder when it is empty;
* delete `coop\` and, optionally, `ms2coop-native.log`, `ms2coop-native.1.log`,
  `ms2coop-native.2.log`, `ms2coop-native.ini` (the DLL writes all of these).

## Play

Host:
```powershell
.\MSCoop.ps1 -Host -GameDir "E:\Games\Mortal Shell II"
```
It starts the relay, prints a **LOBBY ID**, then starts the game.

Friend:
```powershell
.\MSCoop.ps1 -Join -Lobby <LOBBY ID> -GameDir "E:\Games\Mortal Shell II"
```

Both players load into their own save. The other player appears where they are, wearing
their current shell, and walks/runs when they do. Nearby enemies are mirrored, damage both
players deal is applied to both worlds, and an enemy killed by one player dies on the other.

`MSCoop.ps1` writes the mod's session identity for you: `-Host` = player `A` / role `host`,
`-Join` = player `B` / role `guest`. Those two files (`coop\playerid.txt`,
`coop\role.txt`) are what the module pairs on - do not mix a host and a guest id.

## What works (verified, see `docs/QA.md`)

| Feature | State |
|---|---|
| Peer position, smoothed to < 1 uu | PASS (two clients, `dist 0.4, 30/30`) |
| Peer body = their current **shell mesh** (also after a shell swap) | PASS (`SK_DarkForm_Bulk` applied on both sides, `bones=164`) |
| Peer body **walk / run animation**, speed matched | PASS (idle -> walk -> sprint reported with `playing=true` on both clients) |
| Enemy mirror (10 nearest, host-authoritative) | PASS (`20/20 within 30u`) |
| Enemy **damage** sync | PASS (guest wrote 0.5 health, host adopted it) |
| Enemy **death** sync (native death animation) | PASS (`SpartaHealthComponent:StartDeath` on the other client) |
| Peer **weapon** | not visible yet - the weapon path and socket are already synced, spawning it is unfinished and switched off |
| Saves, quest flags, drops, boss state | not synced |

## Known limits

- Both players run their own copy of the zone; this is state mirroring, not netcode.
  Anything the game scripts (cutscenes, boss phases, quest flags) can diverge.
- The peer body is moved, not walked: feet slide a little, no collisions are simulated.
- Animation is the game's own locomotion set; per-shell upper-body/attack animation is not
  replicated.
- Two-machine and long-session stability testing is still open.

## Troubleshooting

- **`ms2coop-native.log`** next to the game exe
  (`<game>\MortalShell2\Binaries\Win64\`) is the module's own log: it must say
  `init: READY` and `CoopMod start`. If it does not exist, the DLL was not loaded - the
  file must be named `dxgi.dll` and sit in that folder, and UE4SS must be disabled
  (`_ue4ss_disabled\`). The previous run is kept as `ms2coop-native.1.log`.
- **`coop\probe.log`** is the co-op module's per-tick log (`[HH:MM:SS][A] ...`);
  `coop\sync.txt` and `coop\heartbeat.txt` show live state and whether the loops tick.
- No second player? Check the relay log in
  `<game>\MortalShell2\Binaries\Win64\coop\`: `relay_A.log` (host) / `relay_B.log`
  (guest) prints `client joined` / `no peer traffic for 3 s`.
- The host's lobby id is printed in `relay_A.log` as `LOBBY=<id>`.
- Both players must run the same game build.
