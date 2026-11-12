# 90 — What was actually verified in the running game

Step 9 of the brief: pick the ten most important objects found in steps 1–8, read their key
properties, **do the action in game**, read them again, and log the values.

Everything below was produced with `ms2-coop\lab.ps1` + the CoopLab mod on the test copy
`E:\Games\_MS2-coop-test`. Three commands were added to `ms2-coop\mod\CoopLab\Scripts\main.lua`
for this step (the shipped `CoopMod` was not touched):

| command | what it does |
|---|---|
| `scan <ClassShortName> [props=A,B] [max=N]` | every live world instance of a class, sorted by distance to the player: full name, distance, location, then each requested property → writes `scan_<Class>.txt` and logs every line |
| `getp <ClassShortName> <Prop>` | one property of every live instance |
| `callf <ClassShortName> <Func> [a,b,c]` | calls a UFunction on up to three live instances and logs `ok=`/`ret=` |

`<ClassShortName>` is what `FindAllOf()` takes: the native class name (`SpartaSaveComponent`) or
the Blueprint class name including the `_C` suffix (`BP_HubDoubleDoor_C`). Instances are filtered
to live world objects (`...:PersistentLevel....`); if that yields nothing the unfiltered list is
used (that is how the save objects, which live under `/Engine/Transient`, show up).

Runs (each: game booted off-screen at 1024×576, `ContinueGame()`, saves snapshot+restore around it):

| run | folder | command file |
|---|---|---|
| verify1 | `local\lab\20261022-063016-verify1` | `research\scripts\verify1.txt` (read-only baselines) |
| verify2 | `local\lab\20261022-063842-verify2` | `research\scripts\verify2.txt` (door / lever / chest / pickup / enemy) |
| verify3b | `local\lab\20261022-065218-verify3b` | `research\scripts\verify3.txt` (boss gates, teleport, save path, kill) |

A first `verify3` attempt died: the game process exited on its own ~1 s after
`phase -> ready`, no dump line, no crash dump, `saves unchanged`. Re-running the identical command
file worked. Worth knowing for anyone automating this: the game occasionally dies at session start,
so a run that logs nothing after `phase -> ready` should simply be repeated.

---

## 1. Summary table

| # | Object | Action performed | Property | before | after | Verdict |
|---|---|---|---|---|---|---|
| 1 | `BP_HubDoubleDoor_C` | `OpenDoor()` | `DoorIsOpen` | `false` | `true` | **changes, repeatable** |
| 1b | `BP_HubDoubleDoor_C` | `CloseDoor()` | `DoorIsOpen` | `false` | `false` | callable, no flip (already closed) |
| 2 | `BP_HubLiftLever_C` | `ActivateLever()` | `TargetFloor` | `0` | `0` | **no observable change** from 11 315 uu away |
| 3 | `BP_Interactable_Chest_Skin_C` | `OnChestOpen()` (3 instances) | `RequiredAmount` | `1` | `1` | callable; no state property exposed |
| 4 | `BP_CurrencyBag_Pickup_C` | `PickUp()` | `SpartaSave.RuntimeData.MyBool` | `false` | `true` | **changes** (this is the world-state record) |
| 4b | `BP_CurrencyBag_Pickup_C` | `PickUp()` | actor location | `49223.8 -130839.3 -42775.8` | `0.0 0.0 0.0` | actor is disabled/consumed |
| 4c | inventory (`SpartaItemInstance`s) | `PickUp()` on the gold bag | instance count | `18` | `19` | **one item added** |
| 5 | `BP_PrologueCheckpoint_C` (14 live) | none — no rest/bind action exists in the lab | `AreaName` | `FText` (opaque) | — | UNKNOWN, see §6 |
| 6 | `BP_Cultist_HUB_Beacon_C` | `SpartaHealthSet.Health` written to 50 % | `Health` / `HealthNormalized` | `75.0` / `1.0` | `37.5` / `0.5` | **changes** |
| 6b | `BP_Cultist_HUB_C` | `SpartaHealthComponent:StartDeath()` | `IsDeadOrDying` | `false` | `true` | **changes** |
| 7 | `BPSO_BossGate_C` (6 live) | none (fight not startable from the lab) | `Unlocked` | `false` | — | state read |
| 7b | `BPSO_FC_FinalBossManager_C` (1) | none | `EndStrandingCompleted`, `ClensedCores`/`TotalCoresNeeded` | `false`, `0`/`3` | — | state read |
| 7c | `BPSO_WordMapObjective_C` (133) | none | `Visible`,`Revealed`,`Completed`,`ShowArea` | e.g. `true,true,false,false` | — | state read |
| 8 | `SpartaItemManagerComponent` | (see 4c) | `ItemList` (`FSpartaItemList`) | — | — | struct not printable; count used instead |
| 9 | player shell (`CharacterMesh0`) | shell swap **not performable**, see §9 | `SkeletalMesh` / `AnimClass` | `SK_DarkForm_Bulk` / `ABP_Player_C` | — | read only |
| 10 | `BP_TeleportLocation_C` (183 live) | `Teleport()` (3 instances) | player world location | `48998.7 -134177.5 -42196.9` | `-65311.0 386290.4 -63265.6` (06:55:05) → back to `48998.7 …` (06:55:25) | **pawn moved, then returned** |

---

## 2. Door — `/Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_HubDoubleDoor.BP_HubDoubleDoor_C`

One live instance in `L_Core_World_01`, 7 932 uu from the player. Its state property is
`DoorIsOpen` (declared on the parent `BP_HubDoor_C`, `BoolProperty [o:0x2e0]`) and it persists through
`BP_HubDoor_C.SpartaSave` (`SpartaSaveComponent`) with `SaveDoorUP()` / `SaveDoorDown()`.

verify2 (`local\lab\20261022-063842-verify2\lab.log`):

```
[06:39:46][A] [1] BP_HubDoubleDoor_C /Game/Sparta/Maps/Core_Levels/L_Core_World_01/_Generated_/CUM674YA7WEQ1OG3M7EUETQNK.L_Core_World_01:PersistentLevel.BP_HubDoubleDoor_C_UAID_D4E98A7EF567D94802_83a1dc45e2a01438_1808979936 dist=7932 loc=56925.7 -134385.0 -42390.0 DoorIsOpen=false
[06:40:14][A] callf BP_HubDoubleDoor_C.OpenDoor[1] BP_HubDoubleDoor_C /Game/Sparta/Maps/Core_Levels/L_Core_World_01/_Generated_/CUM674YA7WEQ1OG3M7EUETQNK.L_Core_World_01:PersistentLevel.BP_HubDoubleDoor_C_UAID_D4E98A7EF567D94802_83a1dc45e2a01438_1808979936 ok=true
[06:40:22][A] [1] BP_HubDoubleDoor_C … dist=7932 loc=56925.7 -134385.0 -42390.0 DoorIsOpen=true
```

verify3b, `CloseDoor()` on a freshly loaded world (`DoorIsOpen` was already `false`, so the call is
confirmed callable but the value cannot flip):

```
[06:53:38][A] [1] BP_HubDoubleDoor_C … DoorIsOpen=false DoorIsOpenName=<userdata>
[06:53:44][A] callf BP_HubDoubleDoor_C.CloseDoor[1] … ok=true
[06:53:50][A] [1] BP_HubDoubleDoor_C … DoorIsOpen=false
```

`DoorIsOpenName` (`NameProperty [o:0x2d8]`) comes back as an opaque FName userdata through this
UE4SS build — the lab prints `<userdata>`; reading the FName text is UNKNOWN.

## 3. Lever — `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_HubLiftLever.BP_HubLiftLever_C`

One live instance, 11 315 uu away, with `TargetFloor` (`IntProperty [o:0x360]`) and `LoadedLift`
(`ObjectProperty of BP_HubLift_C [o:0x368]`). `ActivateLever()` ran successfully but nothing
observable changed within 8 s:

```
[06:40:08][A] [1] BP_HubLiftLever_C … dist=11315 loc=37691.0 -134384.8 -42537.7 TargetFloor=0.0000
[06:40:53][A] callf BP_HubLiftLever_C.ActivateLever[1] … ok=true
[06:41:01][A] [1] BP_HubLiftLever_C … dist=11315 loc=37691.0 -134384.8 -42537.7 TargetFloor=0.0000
```

`LoadedLift` did resolve to a real actor
(`BP_HubLift_C … 2X4U4ZE5Y7L94HLLG9EVNQVBA.L_Core_World_01:PersistentLevel.BP_HubLift_C_UAID_D4E98A7EF5672F1B02_…`),
so the lever knows its lift. Why `TargetFloor` stays 0 is UNKNOWN — the class also has
`CheckTargetFloor()`, `OnLiftStart(TargetFloor: IntProperty)`, `OnLiftEnd()`, `BindOnLiftStart()`,
`BindOnLiftEnd()` and `OnInteract(InteractionComponent, InputAction)`; `GUESS:` the real flow needs
the interaction handler (a player standing at the lever) or the lift's own `OnLiftStart` broadcast.

## 4. Chest — `BP_Interactable_Chest_Skin_C` (parent `BP_Interactable_Chest_General_C`)

8 live instances; the nearest is 9 189 uu away. `OnChestOpen()` (declared on the skin chest) ran ok
on three instances, but the only numeric property on the class (`RequiredAmount`,
`IntProperty [o:0x3f8]`) is configuration, not state, and did not change:

```
[06:41:07][A] callf BP_Interactable_Chest_Skin_C.OnChestOpen[1] … ok=true
[06:41:07][A] callf BP_Interactable_Chest_Skin_C.OnChestOpen[2] … ok=true
[06:41:07][A] callf BP_Interactable_Chest_Skin_C.OnChestOpen[3] … ok=true
[06:41:16][A] [1] BP_Interactable_Chest_Skin_C … dist=9189 loc=39850.7 -133382.8 -42551.0 RequiredAmount=1.0000
```

The chest's persistent state is the `BP_SO_SimpleBool_C` returned by
`GetMySaveData()`, written by `SaveChestState()` / `SaveMyState()` (class dump in
`10-world-state.md`). That object is not reachable by class name from the lab without its owning
actor, so the chest's *state* change could not be read back — UNKNOWN (see §12).

## 5. Item pickup — `BP_CurrencyBag_Pickup_C` (parent `BP_PickupParticleFX_C`)

`CurrencyID = ID_Gold_C`. The pickup's own world-state record is the
`BP_SO_SimpleBool_C` at `SpartaSave.RuntimeData`. This is the cleanest proof of the whole
save mechanism in one run:

```
[06:39:52][A] [1] BP_CurrencyBag_Pickup_C … dist=3395 loc=49223.8 -130839.3 -42775.8 CurrencyID=BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Gold.ID_Gold_C
[06:39:57][A] [2] BP_SO_SimpleBool_C …BP_CurrencyBag_Pickup_C_UAID_D4E98A7EF567C9C102_83a1dc45e2a01438_1764479806.SpartaSave.RuntimeData_2147475099 … MyBool=false
[06:40:03][A] # scan SpartaItemInstance 06:40:03 instances=18
[06:40:28][A] callf BP_CurrencyBag_Pickup_C.PickUp[1] … ok=true
[06:40:37][A] [1] BP_CurrencyBag_Pickup_C … dist=148946 loc=0.0 0.0 0.0 CurrencyID=…ID_Gold.ID_Gold_C
[06:40:42][A] [2] BP_SO_SimpleBool_C …BP_CurrencyBag_Pickup_C_…1764479806.SpartaSave.RuntimeData_2147475099 … MyBool=true
[06:40:48][A] # scan SpartaItemInstance 06:40:48 instances=19
```

i.e. one `PickUp()` call: the save object's `MyBool` flipped `false → true`, the pickup actor's
location became `0,0,0` (disabled), and the player's item-instance count went 18 → 19.

## 6. Checkpoint — `BP_PrologueCheckpoint_C` (parent `/Script/Sparta.SpartaPlayerStart`)

14 live instances. Properties: `OverlapEnabled` (bool), `OverlapQuery` (GameplayTagQuery),
`ApplicationGlobalEvent` (ClassProperty), `ApplicationEventTag` (FGameplayTag), `AreaName` (FText),
plus the functions `GetAreaName()`, `CheckQuery()`, `CachePlayer()`, `EventReceived_…(Payload:
GameplayEventData)` and a sphere-overlap bound event.

```
[06:31:45][A] # scan BP_PrologueCheckpoint_C 06:31:45 instances=14 player=48998.7 -134177.5 -42196.9
[06:31:45][A] [1] BP_PrologueCheckpoint_C …PersistentLevel.BP_PrologueCheckpoint_C_UAID_D4E98A7EF567760C02_2041636700 dist=7002 loc=55996.6 -134391.6 -42271.5 AreaName=FText: 00000249AC11A878
```

**No action was performed**: there is no "rest / bind checkpoint" function on the class and the lab
cannot press a key. `AreaName` is an `FText` that this UE4SS build hands back as an opaque pointer,
so the readable name of the checkpoint is UNKNOWN.

What *is* proven about checkpoint persistence (from the class dump, `10-world-state.md`): the
checkpoint is an `ASpartaPlayerStart`; the player's respawn state is the save object
`/Game/Sparta/SaveObjects/BPSO_PlayerSession.BPSO_PlayerSession_C` with
`StartTransform` (`FTransform [o:0xf0]`), `Health`, `ShellHealth`, `Resolve`,
`HealCurrentCharges`, `ShellReviveProgress`, `IsInDarkform` — two live instances were read in
verify3b (§11).

## 7. Enemy — `BP_Cultist_HUB_Beacon_C` / `BP_Cultist_HUB_C`

Health was written through the game's own attribute set (the mechanism the shipped mod uses):

```
[06:41:22][A] enemies.txt: 3 rows
[06:41:30][A] hurt[BP_Cultist_HUB_C_UAID_D8BBC1D05326704902_83a1dc45e2a01438_1417419333] frac=0.50 ok=true err=nil before=75.0 max=75.0 after=37.5 norm=0.5 dead=false
[06:41:35][A] hcinfo  -> Health = 37.5  MaxHealth = 75.0  HealthNormalized = 0.5  DeathState = 0
                          HealthSet … Health cur=37.5 base=37.5   Poise cur=5.0 base=5.0
```

Death path (verify3b):

```
[06:55:31][A] kill[BP_Cultist_HUB_C_UAID_D8BBC1D05326704902_83a1dc45e2a01438_1417419333] ok=true err=nil deadBefore=false deadAfter=true
```

`enemies` in the same run listed three live characters with their health:

```
  1340.2  hp=75.0  max=75.0  norm=1.0  dead=false BP_Cultist_HUB_Beacon_C
  1992.3  hp=75.0  max=75.0  norm=1.0  dead=false BP_Cultist_HUB_C
  3706.8  hp=75.0  max=75.0  norm=1.0  dead=false BP_NPC_ShellKeeper_C
```

## 8. Boss state — the `BPSO_*` save objects that drive the fights

None of the boss fights can be started from the lab (no player input), so these are **read-only**
observations, but they prove the state lives in flat save objects that any sync layer can read and
write:

```
[06:53:56][A] [1] BPSO_BossGate_C …BP_LevelInstanceSelector_TombZone_VerticalBoss_C_UAID_…_1352038755.SpartaSave.RuntimeData_2147475085 … Unlocked=false      (6 instances, all false)
[06:54:01][A] [1] BPSO_LandingArea_Boss_C …BP_LandingArea_Boss_C_UAID_…_1184030156.SpartaSave.RuntimeData_2147474549 … BeaconRisen=<userdata>   (6 instances)
[06:54:08][A] [1] BPSO_FC_FinalBossManager_C …BP_FinalBossManager_C_UAID_…_1195328269.SpartaSave.RuntimeData_2147477619 … EndStrandingCompleted=false AllCorruptionCoresClensed=false ClensedCores=0.0000 TotalCoresNeeded=3.0000
[06:54:14][A] [1] BPSO_WordMapObjective_C …BP_MapObjective_Spire_C_UAID_…_1731595385.BPC_WorldMapObjective.RuntimeData_2147476814 … Visible=true Revealed=true Completed=false ShowArea=false
[06:54:14][A] [2] BPSO_WordMapObjective_C …BP_LandingArea_C_UAID_…_1955662983.BPC_WorldMapObjective.RuntimeData_2147474826 … Visible=false Revealed=false Completed=false ShowArea=false
```

`BeaconRisen` (`BoolProperty [o:0xb8]`) read back as `<userdata>` — the one property in this batch
that this UE4SS build would not hand over as a Lua boolean. UNKNOWN.

## 9. Inventory — `SpartaItemManagerComponent` is the inventory, not `PlayerInventoryComponent`

Class facts (dump, see `60-items-inventory.md`): `UPlayerInventoryComponent` has **zero**
members; the real container is `USpartaItemManagerComponent` with
`ItemList` (`StructProperty of /Script/Sparta.SpartaItemList [o:0xd8]`),
`OnItemStacksUpdated` / `OnLootPickedUp` (`MulticastInlineDelegateProperty`) and
`AddItemByClass(ItemDef: ClassProperty, StackCount: IntProperty)`,
`RemoveItemStacksByClass(ItemDef: ClassProperty, NumToRemove: IntProperty)`,
`GetTotalItemCountByDefinition(ItemDef: ClassProperty)`, `GetItemStacksByClass(ItemDef: ClassProperty)`,
`GetAllItems()`.

In game, the count of live `SpartaItemInstance` objects (all of them belong to
`BP_PlayerCharacter_C_….ItemManagerComponent`) went **18 → 19** when the gold bag was picked up
(§5). The `ItemList` struct itself printed as an opaque table; the instance count was the usable
proxy.

## 10. Shell — `ChangeShell` needs a GameplayTag, so the lab cannot swap a shell

Readable and verified on the live player pawn:

```
[06:55:05][A] [1] SpartaSkeletalMeshComponent …PersistentLevel.BP_PlayerCharacter_C_2147480019.CharacterMesh0 … SkeletalMesh=SkeletalMesh /Game/Sparta/Characters/Shells/DarkForm_Bulk/Art/Mesh/SK_DarkForm_Bulk.SK_DarkForm_Bulk AnimClass=AnimBlueprintGeneratedClass /Game/Sparta/Characters/Humans/Player/Animations/ABP_Player.ABP_Player_C
```

The swap entry points exist (`raw\CXXHeaderDump\BPFL_Player_Equipment.hpp`):

```
void ChangeShell(FGameplayTag ShellId, bool Save, class UObject* __WorldContext);
void ChangeDarkForm(FGameplayTag DarkFormID, bool Save, class UObject* __WorldContext);
void ActivateDarkForm(class UObject* __WorldContext, bool& Success);
void ChangeShellSkin(FGameplayTag ShellId, FGameplayTag SkinID, bool Save, class UObject* __WorldContext);
```

Two hard blockers, both measured:

1. every one of them takes an `FGameplayTag` (or an out-`Success`), and this UE4SS Lua build has no
   way to construct an `FGameplayTag` from Lua — UNKNOWN how to do it;
2. `BPFL_Player_Equipment_C` is a `BlueprintFunctionLibrary`: `FindAllOf` returns **0 instances**
   (`[06:54:52][A] # scan BPFL_Player_Equipment_C 06:54:52 instances=0`,
   `[06:54:57][A] callf BPFL_Player_Equipment_C: no live instance`), so it cannot be called
   method-style from the lab.

`SpartaPlayerController:SetShell(Shell: ObjectProperty of /Script/Sparta.SpartaPlayerCharacter)` is
the only swap-ish function without a tag argument, but it wants a *shell character actor*; not
attempted (would likely need a spawned shell pawn).

What a sync layer can rely on instead (already used by the shipped mod): the shell is visible as
`SpartaSkeletalMeshComponent.SkeletalMesh` (path above) + `AnimClass`, both readable every tick.

## 11. Level travel — `BP_TeleportLocation_C.Teleport()`

183 live instances. `Teleport()` is a zero-argument function, so the lab could call it:

```
[06:54:27][A] # scan BP_TeleportLocation_C … instances=183 player=48998.7 -134177.5 -42196.9
[06:54:27][A] [1] BP_TeleportLocation_C …_UAID_00D86156EEF00B8D02_1622251253 dist=2294 loc=50910.7 -134839.8 -43278.4 LocationName=<userdata> Category=<userdata>
[06:54:34][A] callf BP_TeleportLocation_C.Teleport[1] … ok=true
[06:54:34][A] callf BP_TeleportLocation_C.Teleport[2] … ok=true
[06:54:34][A] callf BP_TeleportLocation_C.Teleport[3] … ok=true
[06:54:46][A] # scan BP_TeleportLocation_C … player=48998.7 -134177.5 -42196.9     (unchanged 12 s later)
[06:55:05][A] # scan SpartaSkeletalMeshComponent … player=-65311.0 386290.4 -63265.6  (pawn somewhere else)
[06:55:25][A] # scan BPSO_PlayerSession_C … player=48998.7 -134177.5 -42196.9        (back again)
```

Screenshots of the same run show the transition: `step10.png` (right after the three `Teleport()`
calls) has a mean RGB of **14/255** — a fade-to-black — while the neighbouring steps are ~117.
`LocationName` / `Category` are `StrProperty` but read back as `<userdata>`; UNKNOWN.

`GUESS:` the pawn was actually moved to `(-65311.0, 386290.4, -63265.6)` and something (streaming
check → respawn at the checkpoint) put it back within 20 s. Not proven; the only solid facts are the
`ok=true` return, the fade, and the transient location change.

## 12. The save-object plumbing, measured

```
[06:31:52][A] # scan SpartaSaveComponent 06:31:52 instances=471 player=48998.7 -134177.5 -42196.9
[06:31:52][A] [1] SpartaSaveComponent …BP_DungeonTransition_C_UAID_…_1853698048.SpartaSave … bSaveLoaded=true Guid=ScriptStruct /Script/CoreUObject.Guid SaveObjectClass=BlueprintGeneratedClass /Game/Sparta/Core/Interaction/SaveObjects/BP_SO_SimpleBool.BP_SO_SimpleBool_C
[06:31:58][A] # scan BP_SO_SimpleBool_C 06:31:58 instances=27
[06:31:58][A] [1] BP_SO_SimpleBool_C …BP_SplineBasedMovement_C_UAID_…_2104300605.SpartaSave.RuntimeData_2147477651 … MyBool=true
[06:31:58][A] [2] BP_SO_SimpleBool_C …BP_CurrencyBag_Pickup_C_UAID_…_1764479806.SpartaSave.RuntimeData_2147475099 … MyBool=false
```

471 save components in one zone, 27 of them holding a `BP_SO_SimpleBool_C`. That is the scale a
world-state sync layer has to deal with — and every one of them is addressable by the owning actor's
name tail plus `.SpartaSave.RuntimeData`.

`Guid` printed as the *type* (`ScriptStruct /Script/CoreUObject.Guid`) rather than a value: the lab
does not format FGuid. UNKNOWN.

Forcing a save with the cheat manager was accepted but did not populate the session object:

```
[06:55:12][A] console [SaveSessionNow]
[06:55:25][A] [1] BPSO_PlayerSession_C /Engine/Transient.…SpartaSaveSubSystem_2147406…RunSaveObject_2147452782 … Health=0.0000 ShellHealth=0.0000 Resolve=0.0000 HealCurrentCharges=0.0000 IsInDarkform=false
[06:55:25][A] [2] BPSO_PlayerSession_C /Engine/Transient.…SpartaSaveSubSystem_2147406…RuntimeSessionData_2147452781 … Health=0.0000 ShellHealth=0.0000 Resolve=0.0000 HealCurrentCharges=0.0000 IsInDarkform=false
```

Both instances are the `SpartaSaveSubSystem`'s own copy (`RunSaveObject`, `RuntimeSessionData`) and
their `Health` stayed 0.0 before and after `SaveSessionNow`. `GUESS:` those fields are filled from
`ReceiveSpartaPreSave` only on a real save (checkpoint rest / quit), not on this cheat call — UNKNOWN.

## 13. What the lab could NOT do (and why) — the honest gaps

| Object | Why not |
|---|---|
| checkpoint rest / bind | no such UFunction on `BP_PrologueCheckpoint_C`; needs a key press the lab cannot send |
| chest *state* | the state lives in a `BP_SO_SimpleBool_C` reachable only through `GetMySaveData()`; the lab can call functions but cannot pass/return object references reliably, and `RequiredAmount` is config |
| lever floor change | `ActivateLever()` alone did not move `TargetFloor` (§3) |
| shell swap | needs `FGameplayTag` (not constructible from Lua here) and `BPFL_Player_Equipment_C` has 0 instances (§10) |
| boss fight start | needs the player to reach and trigger the arena; nothing scriptable found |
| quest / dialogue progress | `DlgSystem` context objects need player-driven dialogue; not attempted |
| `FName` / `FText` / `FGuid` / some bools | this UE4SS build hands them back as opaque userdata (`<userdata>`); see §2, §6, §8, §11 |

Every one of these is a place where the next round should extend `CoopLab` (e.g. a
`getprop <full object path> <prop>` command that takes an *address/path* instead of a class name, and
a `tag <TagName>` helper that builds an `FGameplayTag` via `UKismetSystemLibrary`), not a place where
the underlying fact is unavailable.
