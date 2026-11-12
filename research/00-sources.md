# 00 — Sources: what the raw data is, how it was made

Research target: **Mortal Shell II**, Unreal Engine 5.6, shipping build, no game netcode.
All data below was collected on this machine on 2026-10-22 with UE4SS **v3.0.1 Beta #0, Git SHA
#83330635** on the *test copy* `E:\Games\_MS2-coop-test` (the real install `E:\Games\Mortal Shell II`
was never touched).

Exe under test: `...\MortalShell2\Binaries\Win64\MortalShell2-Win64-Shipping.exe`,
SHA-256 `BDEDDB33D61E876E94B6C30C8EA198AECFDEFFB557D6F5678C92D1C4C17AAE69`.

Evidence for the toolchain (from `research\runs\20261022-062129-ingame\UE4SS.log`):

```
[2026-10-22 06:21:33.7714674] UE4SS - v3.0.1 Beta #0 - Git SHA #83330635
[2026-10-22 06:21:33.7715474] UE4SS Build Configuration: Game__Shipping__Win64 (MSVC)
[2026-10-22 06:21:34.8974123] [PS] Found BuildConfiguration: Shipping
[2026-10-22 06:21:35.0127951] Using engine version: 5.6
```

The game build id `++Sparta-Depot+Day1+CL92086` used in `ms2-coop\docs\DESIGN.md` is **now
proven** by a dump line: Dumper-7 reported `GameVersion: 5.6.1-0+++Sparta-Depot+Day1+CL92086`
(`research\runs\20261022-175220-dumper7-menu\dumper7.log`), which also fixes the engine
version at `5.6.1` (UE4SS itself only printed `5.6`). Everything else in these reports comes
from the dumps listed below.

---

## 1. The seven raw artefacts

| # | File | Size | Content | Made by |
|---|---|---|---|---|
| A | `raw\ObjectDump_allassets.txt` | 564 882 359 B, **2 170 742 lines** | full UE4SS object dump taken at the **main menu** with `LoadAllAssetsBeforeDumpingObjects = 1` | run `research\runs\20261022-060536-obj` |
| B | `raw\CXXHeaderDump\` | **10 469 files**, 20 804 228 B | UE4SS CXX header generator output (one `.hpp` per class, plus module headers) taken at the **main menu** with `LoadAllAssetsBeforeGeneratingCXXHeaders = 1` | run `research\runs\20261022-061025-cxx` |
| C | `raw\ObjectDump_ingame.txt` | 126 087 083 B, **553 057 lines** | normal object dump taken **in gameplay** (`LoadAllAssets*` = 0) | run `research\runs\20261022-062129-ingame` |
| D | `raw\index.sqlite3`, `raw\index_ingame.sqlite3` | 2 153 654 / 536 682 rows | SQLite indexes of A and C (schema in `scripts\QUERYING.md`) | `scripts\parse_dump.py index` |
| E | `raw\Dumper7\` | **5 365 files**, 128.4 MB, 8 525 classes | **Dumper-7 v7.1.0** (upstream release) SDK at the **main menu**: `SDK\<Package>_classes.hpp`, `_structs.hpp`, `_functions.cpp`, `_parameters.hpp`, `SDK.hpp`, `Assertions.inl` (47 MB) | run `research\runs\20261022-175220-dumper7-menu` |
| F | `raw\Dumper7_allassets\…\CppSDK\` | **18 894 files** (18 893 generated + the `SDKTest.py` copy), 239.4 MB, **16 040 classes** | Dumper-7 SDK in the **same all-assets state as B** (injected right after UE4SS' `DumpAllObjects()` returned); `Assertions.inl` 96.6 MB | run `research\runs\20261022-200712-dumper7-allassets` |
| G | `raw\Dumper7_gameplay\…\CppSDK\` | **7 406 files**, 151.5 MB, 9 441 classes | Dumper-7 SDK taken **in gameplay**, after `ready.txt` | run `research\runs\20261022-201423-dumper7-gameplay` |

`E`–`G` come from the second, independent generator; `05-dumper7-vs-ue4ss.md` records how it is
injected, that `F` agrees with `B` on all **78 740** compared property slots (0 conflicts), and
that the two heavier modes only work with the **locally patched build**
(`tools\dumper7\Dumper-7-patched.dll`, SHA-256
`09D774F705BCD375A46CF25C83929AA75178E99F95C92DDA338E5D170329524D`, patches in
`tools\dumper7-patches.diff`); the upstream release DLL
(`tools\dumper7\Dumper-7.dll`, SHA-256 `5B13729169C69A8FEAE5EB8813977619C3F54729F50362E453FBE68476FBADB9`)
only manages the main menu. Why, exactly, is §3 of that report (GObjects chunk cap → fastfail;
`InitGWorld` full-memory scans → stall; mapping generators after `CppSDK` → apparent hang).


Line counts and per-type histograms of A and C were measured with `scripts\check_dump.py`:

```
file      raw\ObjectDump_allassets.txt      lines 2170742   unmatched 158
file      raw\ObjectDump_ingame.txt         lines  553057   unmatched 116
```

The `unmatched` lines are a dumper formatting quirk: a few hundred parameter lines are written
without the address prefix, e.g.

```
DoubleProperty OutDistancesAlongSpline./Script/Engine.SplineComponent:ConvertSplineToPolyLineWithDistances:OutDistancesAlongSpline
```

The indexer recognises that shape too (`ALT_RE` in `scripts\parse_dump.py`), which is why the
index holds 2 153 654 of the 2 170 742 lines (the remainder are `[n:]`/`[c:]` continuation lines
of the same object, not separate objects).

### How each dump was triggered

`ms2-coop\lab.ps1` (documented in `ms2-coop\docs\HANDOFF.md` §2.3) launches the game parked
off-screen, snapshots/restores the owner's saves, and feeds one command per line into
`<install>\MortalShell2\Binaries\Win64\coop\cmd.txt`, which the CoopLab mod polls.

Two commands were added to `ms2-coop\mod\CoopLab\Scripts\main.lua` for this round (nothing else
in that file was changed; a backup of the previous version is
`local\main.lua.CoopLab.bak-before-dumps`):

| Command | Implementation | Used for |
|---|---|---|
| `dumpobj` | `DumpAllObjects()` | dumps A and C |
| `dumpcxx` | `GenerateSDK()` | dump B |
| `dumpsm` | `DumpStaticMeshes()` | not used |

plus a hold flag: if `<coop>\HOLD_MENU.txt` exists at boot the lab logs
`HOLD_MENU.txt present -> staying on the main menu` and does **not** press `ContinueGame()`, so
the menu-only dumps stay at the menu (UE4SS documents that continuing past the menu after an
all-assets load is unstable). Runner: `scripts\run-dump.ps1` (sets the ini keys, starts
`lab.ps1`, waits for the output to stop growing, kills the game, copies the artefact back,
restores the ini and verifies the saves).

### A — main menu, all assets (the primary source)

`research\runs\20261022-060536-obj\run.log`:

```
[06:05:36] ini: LoadAllAssetsBeforeDumpingObjects=1 LoadAllAssetsBeforeGeneratingCXXHeaders=0
[06:05:37] lab.ps1 started as job 1 (command 'dumpobj@3600')
[06:08:37] target appeared: ...\UE4SS_ObjectDump.txt
[06:08:37] target size now 0 bytes
[06:08:52] target size now 564,882,359 bytes
[06:09:07] done marker + 20s -> killing the game
[06:09:10] copied dump (564,882,359 bytes) to ...\research\raw\ObjectDump_allassets.txt
[06:09:13] ini restored
[06:09:13] saves unchanged
```

and the mod's own log (`coop\lab.log` collected in the same run dir):

```
[06:06:13][A] menu ready after 25s
[06:06:13][A] HOLD_MENU.txt present -> staying on the main menu
[06:06:13][A] phase -> holdmenu
[06:07:41][A] dumpobj: begin (LoadAllAssets is an ini setting)
[06:08:39][A] dumpobj: DumpAllObjects returned
```

Key numbers: 2 170 742 lines, 6467 `Class` objects, **8673 `BlueprintGeneratedClass`**,
686 `WidgetBlueprintGeneratedClass`, 245 `AnimBlueprintGeneratedClass`, 52 415 `Function`
objects, 504 classes in the `/Script/Sparta` module, 78 599 `Package` objects. Top packages by
object count include `/Engine/Transient` (51 367), `/Script/Engine` (30 301) and the menu map
`/Game/Sparta/Maps/Core_Levels/L_Core_World_01` (27 820).

### B — CXX headers

`research\runs\20261022-061025-cxx\run.log`:

```
[06:10:25] ini: LoadAllAssetsBeforeDumpingObjects=0 LoadAllAssetsBeforeGeneratingCXXHeaders=1
[06:13:11] target appeared: ...\CXXHeaderDump
[06:13:27] target size now 20,804,228 bytes
[06:13:58] done marker + 20s -> killing the game
[06:18:43] copied CXXHeaderDump (10,469 files) to ...\research\raw\CXXHeaderDump
```

Layout of `raw\CXXHeaderDump\`: `Sparta.hpp` (8 319 lines — every native `Sparta` class and
struct with member offsets), `Sparta_enums.hpp`, one header per engine module
(`Engine.hpp`, `GameplayAbilities.hpp`, `UMG.hpp`, …) and one small header per Blueprint class
(`BPSO_Gate.hpp`, `WBP_MainMenu.hpp`, …). The generator writes properties first with
`// 0xOFFSET (size: N)` comments, then the functions *without* return types — so **parameter
order and struct layout come from these headers, parameter *types*** come from the object dump,
which lists a function's parameters as properties owned by the `Function` object.

### C — in gameplay

`research\runs\20261022-062129-ingame\run.log`:

```
[06:21:29] ini: LoadAllAssetsBeforeDumpingObjects=0 LoadAllAssetsBeforeGeneratingCXXHeaders=0
[06:22:45] target appeared: ...\UE4SS_ObjectDump.txt  (126,087,083 bytes)
[06:23:00] killed MortalShell2-Win64-Shipping (39396)
[06:23:02] copied dump (126,087,083 bytes) to ...\research\raw\ObjectDump_ingame.txt
```

The world that was live (from the collected `coop\lab.log`):

```
[06:22:0x][A] ContinueGame ok=true
[06:22:0x][A] gameplay ready: BP_PlayerCharacter_C /Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01:PersistentLevel.BP_PlayerCharacter_C_2147479936 at 49003.1 -134183.8 -42148.9
```

i.e. the save's resumed checkpoint in `/Game/Sparta/Maps/Core_Levels/L_Core_World_01`, at
world position `(49003.1, -134183.8, -42148.9)`. Nobody played the character in this run — the
pawn stands where `ContinueGame()` put it. The dump therefore shows the **loaded** world: the
level package contributes 30 621 objects, but actors outside the streaming radius / not yet
spawned are not in it. That is the honest limitation of C.

### D — indexes

```
python scripts\parse_dump.py index raw\ObjectDump_allassets.txt raw\index.sqlite3
python scripts\parse_dump.py index raw\ObjectDump_ingame.txt   raw\index_ingame.sqlite3
```

Schema, query recipes and the "how to read a function" rules: `scripts\QUERYING.md`.
Helper command files: `scripts\q\*.sql`.

---

## 2. What was superseded

Before this round the test install held an `UE4SS_ObjectDump.txt` from the previous working
session (66 699 258 B, 338 629 lines, written 2026-10-22 00:37). It was **overwritten** by run A.
The slices the earlier session cut from it are still in `local\dump-analysis\` (`anim_assets.txt`,
`combat_funcs.txt`, `enemy_bp.txt`, `player_bp_funcs.txt`, `sparta_attr.txt`, …); they were used
only as a *map* of where to look, never as evidence — every fact in `research\1x`-`7x` is cited
from A, B or C.

---

## 3. Save protection (rule: never touch the owner's saves)

Real saves live in `%LOCALAPPDATA%\MortalShell2\Saved\SaveGames` — they do **not** follow an
overridden `LOCALAPPDATA` (see `HANDOFF.md` §5.13), so every run backs them up and verifies them.

| When | Backup | Result |
|---|---|---|
| before the first run | `local\save-backups\20261022-060155\` (+ `hashes-before.txt`) | baseline |
| each run | `local\save-backups\<stamp>-rundump-<mode>\` | compared after the run |

Baseline hashes (the state the owner's folder was in when this round started):

```
0C94B2DCAA2C48E39EEE51E7A506A07B03C4A981DC135E7A5C2D2C74B7E1D810  MetaSaveGame.sav          (2198 B)
47095B2B5E72D5B302A6FD22354703722850308560E85183A6791602F603B7FD  SpartaGameSettings.sav    (28611 B)
96EDA8577BBDBFFC89C21309F2D98993BEAFD47C02342E3D1E2F721E28BAC49A  WorldState_0.sav          (67330 B)
```

`0C94B2DC…` for `MetaSaveGame.sav` is bit-identical to the pre-session backup
`local\save-backups\backup-20261022-001113\SaveGames\MetaSaveGame.sav`, i.e. the folder was in
its pristine state at 06:01.

Measured behaviour of the game (this is what a co-op harness has to cope with):

1. **The main-menu dumps did not touch the saves** — runs A and B both logged `saves unchanged`.
2. **Entering gameplay rewrites `MetaSaveGame.sav`** (2198 B → 2306 B, hash `CAEF263B…`) and
   creates rotating `WorldState_Backup_0_<n>.sav` copies (byte-identical to `WorldState_0.sav`).
   A run that starts the game and enters the world will therefore always dirty these files.
3. A run that is killed *between* the game writing and the harness comparing can miss the change
   (one earlier 70-second run reported `unchanged` and still left `MetaSaveGame.sav` modified);
   the reliable pattern is: compare, restore, **then compare again**, and also delete files that
   did not exist before.
4. All of the above was undone: `MetaSaveGame.sav` was restored from the pristine copy,
   `WorldState_Backup_0_0.sav` / `WorldState_Backup_0_1.sav` were moved out of the folder to
   `local\save-backups\20261022-062129-rundump-ingame\extras-created-by-game\`, and the folder
   now matches the baseline hashes exactly again:

```
0C94B2DCAA2C48E39EEE51E7A506A07B03C4A981DC135E7A5C2D2C74B7E1D810  MetaSaveGame.sav
47095B2B5E72D5B302A6FD22354703722850308560E85183A6791602F603B7FD  SpartaGameSettings.sav
96EDA8577BBDBFFC89C21309F2D98993BEAFD47C02342E3D1E2F721E28BAC49A  WorldState_0.sav
```

---

## 4. Environment state after this round

* `UE4SS-settings.ini` in `_MS2-coop-test` and `_MS2-coop-b`: both `LoadAllAssetsBefore*` keys are
  back to `0` (verified after the runs). `MaxMemoryUsageDuringAssetLoading` stayed at its
  pre-existing value `80`.
* No `MortalShell2-Win64-Shipping`, `CrashReportClient` or `cooprelay.py` process was left running
  (checked at the end of every run and again at the end of the session).
* The real install `E:\Games\Mortal Shell II` was not read or written by this round.
* Crash dumps from the earlier session are still lying in the two test installs
  (`crash_2026_10_22_*.dmp`); this round produced none.

## 5. Facts about the dumps that will bite anyone who re-uses them

1. The all-assets dump (A) was taken **at the main menu**, so `/Engine/Transient` is full of
   menu-session objects and the only map objects in it are the menu map. The gameplay dump (C) is
   the one to use for "what actually exists in the world".
2. Objects are keyed by **name tail**, and this game names spawned actors deterministically
   (`BP_PlayerCharacter_C_2147479936` keeps the same number across launches — see
   `ms2-coop\docs\DESIGN.md`), which is what the shipped mod relies on.
3. `Class` vs `BlueprintGeneratedClass`: every `/Game/...` class is a
   `BlueprintGeneratedClass` (or `WidgetBlueprintGeneratedClass` / `AnimBlueprintGeneratedClass`),
   never a `Class`. The `cls` view in the SQLite index covers all of them.
4. A Blueprint function's parameter list contains blueprint **compiler temporaries**
   (`Temp_bool_Variable`, `CallFunc_…`, `K2Node_…`, `ExecuteUbergraph_…`). The reports filter
   them out and say so where it matters.
5. Blueprint `Function` objects expose the same parameters as the C++ reflection data, but the
   dump does not mark out-parameters; a parameter named `ReturnValue` is the return value, and
   `CXXHeaderDump\*.hpp` gives the declaration order to cross-check.
