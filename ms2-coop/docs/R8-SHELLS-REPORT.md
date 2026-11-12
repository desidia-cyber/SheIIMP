# Round 8 — appearance sync: different shells, skins, clothing, dark form, unloaded masks

Agent `r8-shells`. Workspace `G:\Freespace\shell_coop\wt\r8-shells`.
Installs: host A `E:\Games\_MS2-coop-k`, guest B `E:\Games\_MS2-coop-l`, relay port 47150.
Every run below is a **locked** run (`LOCK acquired by r8-shells` … `LOCK released by r8-shells`).

**Verdict: all four on-screen items pass — different shells, real skin swaps, clothing and
masks now reach the host's puppet, and the identity (skin materials + mask + robe) matches
between B's own character and A's rendering of it. The suite's `APPEAR` verdict is PASS in
every run (5/5). The full suite still ends `RESULT: FAIL` on `MIRROR` and `STREAM` — proven
below not to be the appearance lane (it fails identically with that whole lane switched off,
and both verdicts pass in the reduced run that skips the phases before them).**

---

## 1. Root cause

Four defects, three of them found by running the lab (not by reading):

1. **Nothing ever changed the shell/skin/mask/clothing.**  `run-two.ps1`'s `LOOK` verdict
   only called `lookadd` (an attachable put on the pawn by hand), so the equipment path was
   untested (`docs/HANDOFF.md` §20 says so verbatim: "Not verified: different shells … skins
   with real material swaps, clothing").
2. **The receiver had no asset loader.**  `net::Find(path, true)`'s `requestLoad` arm ended
   in a log line (`LOAD_ON.txt is set but the native build has no asset loader`);
   `puppet::RequestGameLoad()` only re-queried `FindObject`, so a mesh this PC never loaded
   could never arrive (`sync.txt lookmissing` retried for ever).
3. **The puppet body went through the same hole** (`ApplyBodyMeshImpl` → `FindAsset`), so a
   peer in a different shell could not get its body mesh.
4. **[found in the lab] The equipment writers do not spawn anything.**  Lab run 3:
   `ChangeDarkFormMask` / `ChangeDarkFormClothing` / `ChangeDarkFormSkin` all answered
   `ok=true` and the pawn's attachable list stayed empty (`peer look applied … (0 parts)`).
   The write reaches the save object; the actor that is *rendered* is spawned by the pawn's
   own `UBPC_AttachableItems_C` (`ChangeMask`/`AddMask`/`ChangeClothing`/`AddClothing` —
   `research/raw/CXXHeaderDump/BPC_AttachableItems.hpp`), the same component `lookadd` →
   `AddItem` uses and the LOOK verdict proves. **That was the missing piece.**

## 2. Diff summary (files + functions)

| File | Change |
|---|---|
| `native/src/coop_load.cpp` | **NEW (item 5).** `load::Asset(path)`: `KismetSystemLibrary::MakeSoftObjectPath(FString)` → `Conv_SoftObjPathToSoftObjRef()` → `LoadAsset_Blocking()` on the CDO, values moved by the width the engine reports, `Conv_SoftObjectReferenceToObject` fallback. Rate limited (1/s, 3 tries/path, 48 paths), counters, `load::Reset()`. |
| `native/src/coop_equip.cpp` | **NEW (items 1–4).** `equip::Cmd()`: the game's writers (`ChangeShell`, `Change*Skin`, `Change*Mask`, `Change*Clothing`, `ActivateDarkForm`, `Unlock*`, `Swap*Materials`) **plus the attachable component** (`ChangeMask`/`AddMask`/`ChangeClothing`/`AddClothing`, `FindMaskClass`, `FindClothingClass`) **plus `ClosePopups()`** (`RemoveFromParent` on the *on-screen* notify/tutorial widgets — the "LINGERING SHADOW" card). Verbs: `catalogue`, `dump`, `state`, `shell`, `skin`, `mask`, `clothing`, `dfmask`, `dfclothing`, `darkform`, `swapmat`, `mesh`, `additem`, `popup`, `attach`, `load`. All writes `Save=false`. |
| `native/src/coop_probe.cpp` | `PupDump`: prints each mesh's **slot materials** (`GetNumMaterials`/`GetMaterial`) — a skin is a material swap and there was no way to see it; `pupdump self` now marks the SELF pawn's body mesh (it marked the puppet's, so self printed no materials). |
| `native/src/coop_net.cpp` | `net::Find()`: `requestLoad` calls `load::Asset()`; `Toggles::Load()` row `assetLoad` ← `APPEAR_OFF.txt`; router: `equip`. |
| `native/src/coop_puppet.cpp` | `RequestGameLoad()`: real synchronous `load::Asset()` instead of the queued `FindObject`. |
| `native/coop_state.h` | `Toggles::assetLoad`; declarations of `coop::load` / `coop::equip`. |
| `ms2-coop/run-two.ps1` | new **APPEAR** verdict (mask → clothing → skin → shell guest→host round trip, each waiting for `look` changed + `lookapplied == look` + `lookmissing == 0` + the host's part count grew), the dark-form restore, `$pass = $pass -and $appearOk`, an `APPEAR_SKIP.txt` A/B switch, and the block placed **after** every other verdict so its pawn swap cannot perturb another lane. |
| `ms2-coop/visual-two.ps1` | `Shot()` no longer aborts a run when a client's `MainWindowHandle` is the empty string (a dead client used to kill a run before any verdict). |
| `lab-appear.ps1`, `run-two-shells.ps1`, `make-sheet.ps1` | workspace helpers: lock (1 s polling, same atomic mkdir) + deploy + one lab pass / the suite (`-Off`, `-StreamOnly`) / a contact sheet. |

Kill switch: **`APPEAR_OFF.txt`** → `net::Find` behaves exactly like round 7 and
`RequestGameLoad` falls back to the object-array re-query.

## 3. Bugs found by the lab (all fixed)

1. `next` picked a tag from `EquipmentUnlockState` **without filtering by kind**, so
   `equip mask next` handed `ChangeDarkFormMask` the tag `Weapon.HadernsSword` — accepted
   with `ok=true`, silently doing nothing (probe_B 15:52:12).
2. The mask/clothing census read `ActiveItems`; masks live in **`ActiveMasks`**, so the code
   thought nothing had spawned and added a SECOND mask in slot 1 (`masks=2`).
3. `pupdump self` printed no materials (it marked the *puppet's* mesh).
4. `ClosePopups` removed every matching widget instance, including off-screen templates —
   now only `IsInViewport` ones (one client died with the build's known `0xC0000374` in
   ntdll right after a popup sweep; three other runs with the same sweep survived, but there
   is no reason to add a second route to that crash).

## 4. Test commands and verdict lines (all locked runs)

Build (new modules are picked up by `build.bat`'s `src\*.cpp` glob):

```
cmd /c G:\Freespace\shell_coop\wt\r8-shells\ms2-coop\native\build.bat test
coop_policy: 112 checks passed, 0 failed
PASS: all 4 patterns match exactly once and resolve to the recovered addresses
Built: ...\build\dxgi.dll
```
(The `[exit code: 1]` the tool prints next to that command is a pipeline artefact of piping
`build.bat` through `Select-Object`; its success path is `:tests_skipped … exit /b 0`.)

Tag catalogue (run 2, `...\lab\20261026-155053-t2-catalogue`):

```
[B] equip catalogue shell: … CharacterId.Player.Shell.{Tiel,Genessa,Necrophage.Default,Smert,Eredrim,Thorn,Gragu,KnightLady,Solomon,Harros}
[B] equip catalogue mask: Sparta.Item.Mask.{FlowerCrown,Shell.Gragu.Helmet,BagHead,Mango,Frog}
[B] equip catalogue clothing: Sparta.Item.Clothing.Robe
[B] equip catalogue skin: Sparta.Item.Skin.Harbinger.{Skeleton,Obsidian,Red,Cosmic}
```

**Run t3-fixed** (`...\lab\20261026-155827-t3-fixed`) — item 1 + the loader:

```
B  equip: ChangeShell(CharacterId.Player.Shell.Tiel) Save=false ok=true   → character=CharacterId.Player.Shell.Tiel
A  puppet body <- /Game/Sparta/Characters/Shells/Tiel/Art/Mesh/SK_Tiel.SK_Tiel : bones=104
A  asset not loaded yet: /Game/Sparta/Weapons/Player/TielDagger/Art/Mesh/SM_TielDagger.SM_TielDagger
A  load: …/SM_TielDagger.SM_TielDagger -> StaticMesh (1 loaded, 0 failed)
```

**Run t4-maskcloak** (`...\lab\20261026-161248-t4-maskcloak`) — the component fix:

```
A  look part on puppet: SM_DarkForm_Bulk_FlowerCrown @Socket_CrownFlower   → peer look applied 038dd00f (1 parts)
A  look part on puppet: SK_DarkForm_Bulk_OnlyRobe @Root                    → peer look applied f456cbce (2 parts)
A  peer look applied cbe7308e (2 parts)                                    ← the skin's bm= changed
B  equip popup: WBP_Notify_Tutorial_C … RemoveFromParent=true (30 widgets) ← B's window usable again
```

**Run t5-baghead** (`...\lab\20261026-162307-t5-baghead`) — skin materials, both sides:

```
A [16:25:47] pupdump: mats=6 [MI_DarkBro_Cosmic_LegRight,MI_DarkBro_Cosmic_Arms,MI_DarkBro_Cosmic_LegLeft,MI_DarkBro_Cosmic_Back,MI_DarkBro_Cosmic_Chest,MI_DarkBro_Cosmic_Head]   ← the PUPPET (B's look as A renders it)
B [16:25:47] pupdump: mats=6 [MI_DarkBro_LegR_01,…]                                        ← A's body as B renders it (A wears nothing)
A            look part on puppet: SM_DarkForm_Bulk_Baghead_Bag_head @Head
```

**Run t6-gragu** (`...\lab\20261026-165530-t6-gragu`) — the clean pair:

```
B [16:57:01] pupdump: mats=6 [MI_DarkBro_Obsidian_LegR_01,…]        ← B's OWN character (pupdump self)
A [16:58:22] pupdump: mats=6 [MI_DarkBro_Obsidian_LegR_01,…]        ← the same six on A's puppet
A [16:56:52] look part on puppet: SM_GraguHelmet_head @Head
A [16:56:55] look part on puppet: SK_DarkForm_Bulk_OnlyRobe @Root   → peer look applied 82bbe262 (2 parts)
A [16:58:22] pupdump: mats=1 [MI_Gragu_Helmet_01]                   ← the helmet's material on the puppet
```

## 5. The five items

### 1. Two players wearing DIFFERENT shells — **VERIFIED**
B `ChangeShell(CharacterId.Player.Shell.Tiel)` → B is `CharacterId.Player.Shell.Tiel`; A's
puppet body becomes `…/Tiel/Art/Mesh/SK_Tiel.SK_Tiel` (bones=104, was `SK_DarkForm_Bulk`,
bones=164). Screenshots: `A-sees-B-dir*.png`, `A-after-cmds4.png` in
`...\lab\20261026-155827-t3-fixed\` (slim Tiel + TielDagger) vs
`...\lab\20261026-143943-t1-dumpall\A-after-cmds1.png` (bulky dark form).

### 2. Real skin swaps at runtime — **VERIFIED**
`ChangeDarkFormSkin(…Harbinger.Cosmic/Obsidian)` + `SwapDarkFormMaterials(mesh=CharacterMesh0)`;
A's puppet carries the skin's materials, and in t6 **B's own character and A's puppet have
the identical six materials** (`MI_DarkBro_Obsidian_*`). The wire carries it because a skin
is a body-material swap and the look signature starts with `bm=`.

### 3. Clothing / armour pieces — **VERIFIED (it was a real bug, now fixed)**
`ChangeDarkFormClothing` alone did nothing (run 3: 0 parts). With the attachable component,
`ChangeClothing(Sparta.Item.Clothing.Robe, cat=0)` spawns `SK_DarkForm_Bulk_OnlyRobe` and A
shows it (`look part on puppet: SK_DarkForm_Bulk_OnlyRobe @Root`, `(2 parts)`). So clothing
**does** exist in this save once `UnlockClothing(Save=false)` + the component path run.

### 4. Dark-form extras — **VERIFIED for materials + extra meshes; VFX NOT POSSIBLE**
The test character *is* the dark form, so "while transformed" is the baseline, and all three
extra-visual routes were demonstrated on it: the material swap (item 2), the mask actor
(items 3/5) and the robe (item 3) all render on the dark-form body on the host. The
transformation itself flips the body both ways (t3 `SK_DarkForm_Bulk` → `SK_Tiel`; the APPEAR
block switches back with `ActivateDarkForm`, `APPEAR back:` shows the body back on both).
**VFX is not possible on the current wire**: `look` carries mesh components and their slot
materials only — no particle/niagara channel, and the sender enumerates mesh components.

### 5. A peer mask whose mesh this PC never loaded — **VERIFIED (loader), mask clause qualified**
The loader is proven end-to-end on a mesh the host had never loaded (t3:
`asset not loaded yet: …/SM_TielDagger` → `load: … -> StaticMesh (1 loaded, 0 failed)`), the
same `net::Find(path, true)` call every mask part uses; it also loaded a dialogue asset on
demand (`…/DLG_HubCultist02_Greeting -> DlgDialogue`), which is why the DIALOG lane stays
green with the loader on.
For **masks specifically** the "never loaded" precondition cannot be produced in this build:
all five mask meshes are resident before they are worn (neither BagHead nor the Gragu helmet
logged `asset not loaded yet`, yet both attached). So the mask half is verified as "the host
shows the peer's mask"; the *loading* half is verified on meshes that genuinely were absent.

## 6. Screenshots (all under `G:\Freespace\shell_coop\local\lab\`)

| File | What it shows |
|---|---|
| `20261026-162307-t5-baghead\compare-AseesB-vs-Bself.png` | **the pair**: left = A's window (B's puppet: bag-head mask + Cosmic materials), right = B's own window — same mask, same sheen |
| `20261026-162307-t5-baghead\contact-sheet.png` | all 25 shots of that run |
| `20261026-165530-t6-gragu\contact-sheet.png` | Gragu helmet + robe + Obsidian skin; `B-sees-A-*` shows the character (popup closed) |
| `20261026-161248-t4-maskcloak\contact-sheet.png` | first run where mask + robe + skin reached the puppet |
| `20261026-155827-t3-fixed\contact-sheet.png` | the different-shell run (Tiel) |
| `20261026-143943-t1-dumpall\contact-sheet.png` | baseline (nothing changed) for comparison |

## 7. The suite

The new verdict, in every run:

```
=== APPEAR: B before shell=CharacterId.Player.Darkform.StrongOne skin=None mask=None clothing=None dfmask=None unlocks=4
=== APPEAR mask: B amasks=0->1 ; A applied=21c05385 parts=0->1 missing=0 loads=0->0 on=True
=== APPEAR clothing: B aclothing=0->1 ; A applied=8fce8184 parts=1->2 missing=0 on=True
=== APPEAR skin: B dfskin=None->Sparta.Item.Skin.Harbinger.Obsidian ; A applied=29443644 missing=0 on=True
=== APPEAR shell: B body=…/SK_DarkForm_Bulk.SK_DarkForm_Bulk -> …/SK_Tiel.SK_Tiel shell=CharacterId.Player.Shell.Tiel ;
                  A peerbody=…/SK_Tiel.SK_Tiel applied=e69075f2 missing=0
=== APPEAR back: B body=…/SK_DarkForm_Bulk… ; A peerbody=…/SK_DarkForm_Bulk… missing=0
=== APPEAR: PASS (mask=True clothing=True skin=True shell=True)
```

| Run | Command | RESULT | Red verdicts |
|---|---|---|---|
| 18:12 (`run-two-20261026-181211.log`) | full suite | FAIL | MIRROR, STREAM, NPCFAR |
| 18:32 (`…-183255.log`) | full suite | FAIL | MIRROR, STREAM, NPCFAR |
| 18:44 (`…-184441.log`) | full suite, APPEAR moved to the end | FAIL | MIRROR, STREAM |
| 19:03 (`…-190310.log`) | **baseline**: `-Off 'APPEAR_OFF.txt,APPEAR_SKIP.txt'` (no loader, no equip) | FAIL | MIRROR, STREAM — **same two, appearance lane entirely off** |
| 19:13 (`…-191358.log`) | full suite | FAIL | MIRROR, STREAM |
| 19:31 (`…-193134.log`) | **`-StreamOnly`** (all lanes incl. APPEAR, phases before MIRROR skipped) | **PASS** | — (MIRROR, STREAM, NPCFAR all PASS) |
| 19:53 (`…-195349.log`) | full suite | FAIL | MIRROR, STREAM |

What that table says, precisely:

* **`APPEAR` is PASS in 5/5 runs**, including the reduced run.
* The two red verdicts are **not the appearance lane**: the baseline run at 19:03 has the
  whole lane switched off (`APPEAR_OFF.txt` + `APPEAR_SKIP.txt`) and fails exactly the same
  `MIRROR` and `STREAM`.
* They are **sequence-dependent, not absolute**: in `-StreamOnly` — which still runs LOOK,
  APPEAR, SPAWN, MIRROR, AITARGET, STREAM, NPCFAR and the rest of the stream block —
  every one of them is PASS and the suite ends `RESULT: PASS`.
* The failing shape is specific: `MIRROR … spawnask:8 spawnok:0` (the host's spawn requests
  for a mirrored enemy all fail) and `STREAM … cells around it loaded=false, static meshes
  0 -> 2`. Both come **after** the DEATH / DROPS / WORLD / PROGRESS / QUEST / CHEST /
  STORE_A2 phases in the full sequence, i.e. the enemy-spawn and world-streaming subsystems
  are in a state those phases leave behind. That is where the next investigation belongs.

## 8. Open items

1. **MIRROR / STREAM in the full sequence** (owner: the spawn/stream lanes, not this one):
   they fail after the death+world phases and pass without them; the same two fail with the
   appearance lane off. `-StreamOnly` is green end to end.
2. **VFX while transformed** — not on the wire (new key + Niagara component class).
3. **A mask whose mesh is absent** cannot be constructed in this build (all mask meshes
   resident at boot); the loader is proven on other absent meshes.
4. `equip` is lab-only code; the only *behaviour* change on the sync path is the loader, and
   it has `APPEAR_OFF.txt`. `APPEAR_SKIP.txt` only affects the harness.
5. One client died once with `0xC0000374` in ntdll (the build's known Steam crash) right after
   a popup sweep; popup removal is now limited to on-screen widgets, and 5 later runs with the
   same commands were clean.
