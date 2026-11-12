# 40 (part 2) -- Player abilities: sidearms, shells, tarstones, artifacts, consumables, interactions, traversal

Continuation of `40-player-actions.md`. Sections: sec.9 sidearms/ranged, sec.10 shells + shell state
abilities, sec.11 tarstones / weapon abilities / artifacts / upgrades, sec.12 consumables (PP items, heal
charges, item use), sec.13 interactions and memories, sec.14 traversal / transformation / fast travel / rest,
sec.15 consolidated sync-hook list, sec.16 UNKNOWN list.

Same conventions as part 1: `--` / `none` = no own property (value inherited), offsets exactly as the dump
prints them, every table followed by verbatim dump lines.

**How to read the enumeration tables below.** Each table is generated from `cls` with a folder `LIKE`
filter, so it lists **every class in that folder**, not only the `GA_*` abilities: the item definitions
(`ID_*`), item fragments (`IF_*`), gameplay effects (`GE_*`) and anim notifies (`AN_*`, `ANS_*`) that live
next to the abilities are listed too, because they are the data an ability references. The `GA_*` subset
per folder is:

| Folder | `GA_*` classes | all classes listed |
|---|---|---|
| `/Game/Sparta/Core/Player/Ability/Sidearm/` (sec.9) | 33 | 37 |
| `/Game/Sparta/Core/Characters/Player/` (sec.10) | 57 | 142 |
| `/Game/Sparta/Core/Tarstones/` (sec.11.1) | 49 | 166 |
| `/Game/Sparta/Core/Player/Artifacts/` (sec.11.2) | 25 | 75 |
| `/Game/Sparta/Core/Player/Upgrades/` (sec.11.3) | 70 | 70 listed (folder has 414 classes; the other 344 are item definitions / fragments / effects, covered by the items report) |
| `/Game/Sparta/Items/Pickups/PermProgression/` (sec.12) | 20 | 90 |
| `/Game/Sparta/Core/Player/Ability/Interactions/` (sec.13) | 193 | 193 |
| `/Game/Sparta/Core/Player/Ability/Input/` (part 1 sec.8) | 52 | 52 |
| `/Game/Sparta/Core/Player/Ability/Weapons/` (part 1 sec.6) | 181 | 190 |

---

## 9. Sidearms / ranged weapons -- `/Game/Sparta/Core/Player/Ability/Sidearm/`

Two ability families per sidearm:
`GA_<Weapon>PrimaryActivation`/`...SecondaryActivation` = the "fire" ability
(parent `GA_SidearmActivationBase_C`, a `USpartaGameplayAbility`) and
`GA_<Weapon>Attack_Primary` = the animation/projectile ability
(parent `GA_SidearmRangedAttackBase_C`, a `GA_SpartaBase_C`, i.e. an **attack** ability).
`GA_Sidearm_Draw` / `GA_Sidearm_Stow` are `GA_PlayMontageBase_C` children.

### 9.1 Base-ladder properties

`GA_SidearmActivationBase_C` (parent `GA_SpartaAbility_C`) -- 13 own properties:

| Property | Type | Offset |
|---|---|---|
| `SidearmAbility` | `ObjectProperty of .../Sidearm/GA_SidearmRangedAttackBase.GA_SidearmRangedAttackBase_C` | `0x518` |
| `ProjectileObject` | `ObjectProperty of .../Core/Projectiles/BP_ProjectileBase.BP_ProjectileBase_C` | `0x520` |
| `Counter`, `CounterMax` | `IntProperty` | `0x528`, `0x52C` |
| `CrossfireRate` | `FloatProperty` | `0x530` |
| `CrossfireFunction` | `StrProperty` | `0x538` |
| `SendActivationEvent` | `BoolProperty` | `0x548` |
| `Projectile` | `GameplayTag` | `0x54C` |
| `MyEventData` | `GameplayEventData` | `0x558` |
| `bHasBuff` | `BoolProperty` | `0x608` |
| `DefaultHitscanQuery` | `ArrayProperty` | `0x610` |
| `ForceFeedback` | `ObjectProperty of Engine.ForceFeedbackEffect` | `0x620` |
| `PayloadPreventionQuery` | `GameplayTagQuery` | `0x628` |

`GA_SidearmRangedAttackBase_C` (parent `GA_SpartaBase_C`) -- 14 own properties:

| Property | Type | Offset |
|---|---|---|
| `ShootingMontage` | `ObjectProperty of Engine.AnimMontage` | `0x5E0` |
| `ShootingPlayRate` | `FloatProperty` | `0x5E8` |
| `bRandomizeTargetBone`, `bDebugAimDirection` | `BoolProperty` | `0x5EC`, `0x5ED` |
| `TargetBones` | `ArrayProperty` | `0x5F0` |
| `WeaponSlot` | `GameplayTag` | `0x600` |
| `AbilityWeapon` | `ObjectProperty of .../Core/Weapons/WP_WeaponBase.WP_WeaponBase_C` | `0x608` |
| `ProjectileObject` | `ObjectProperty of .../BP_ProjectileBase.BP_ProjectileBase_C` | `0x610` |
| `ActivationEventTag` | `GameplayTag` | `0x618` |
| `ActiveEffectHandle` | `ActiveGameplayEffectHandle` | `0x620` |
| `RangedWeaponComponent` | `ObjectProperty of Sparta.SpartaWeaponComponent_Ranged` | `0x628` |
| `LastStrikeDirection` | `EnumProperty of Sparta.ESpartaStrikeDirection` | `0x630` |
| `PushedAudioData` | `ObjectProperty of Sparta.SpartaWeaponAudioData` | `0x638` |
| `ListenToReactionTypes` | `GameplayTagContainer` | `0x640` |

`GA_SidearmRangedBurstAttackBase_C` (parent `GA_SidearmRangedAttackBase_C`):
`ReleaseEventTag: GameplayTag` `0x668`.

`GA_SidearmRangedChargedAttackBase_C` (parent `GA_SidearmRangedAttackBase_C`) -- 16 own properties:
`ChargeTimerFunctionName: StrProperty` `0x668`, `ChargeAttackTimer: TimerHandle` `0x678`,
`ChargeTimeRange: Vector2D` `0x680`, `ChargeDamageMultiplier: Vector2D` `0x690`,
`ChargingMontage: ObjectProperty of Engine.AnimMontage` `0x6A0`, `ChargingPlayRate: float` `0x6A8`,
`ReleaseEventTag: GameplayTag` `0x6AC`, `ChargeTime: float` `0x6B4`, `HasMinCharge: bool` `0x6B8`,
`HasMaxCharge: bool` `0x6B9`, `ChargingReleaseEventTask: AbilityTask_WaitGameplayEvent` `0x6C0`,
`ChargingForceFeedback: ObjectProperty of Engine.ForceFeedbackComponent` `0x6C8`,
`ForceFeedback_Charging` `0x6D0`, `LoopingChargedForceFeedback` `0x6D8`,
`ForceFeedback_Charged` `0x6E0`, `ForceFeedback_Charged_Loop` `0x6E8`.

`GA_SidearmSecondaryActivation_C` (parent `GA_SidearmActivationBase_C`):
`CustomProjectileClass: ClassProperty` `0x678`.

Ammo lives in the attribute set, not the ability: `USidearmAttributeSet:CurrentAmmo/MaxAmmo/ReloadDuration`
(sec.2.5). `ChargeTime` + `HasMinCharge`/`HasMaxCharge` (`0x6B4`..`0x6B9`) is the charged-shot state a
sync layer must replicate for the trebuchaxe/ballistazooka.

### 9.2 Enumeration -- every sidearm ability class

| Class | Parent | Own props |
|---|---|---|
| `/Game/Sparta/Core/Player/Ability/Sidearm/Ballistazooka/GA_BallistazookaAttack_Primary.GA_BallistazookaAttack_Primary_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmRangedAttackBase.GA_SidearmRangedAttackBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Sidearm/Ballistazooka/GA_BallistazookaPrimaryActivation.GA_BallistazookaPrimaryActivation_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmActivationBase.GA_SidearmActivationBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Sidearm/Crossbow/GA_CrossbowAttack_2H_Primary.GA_CrossbowAttack_2H_Primary_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmRangedAttackBase.GA_SidearmRangedAttackBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Sidearm/Crossbow/GA_CrossbowPrimaryActivation.GA_CrossbowPrimaryActivation_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmActivationBase.GA_SidearmActivationBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Sidearm/CursedChild/BP_CursedChild_Visualizer.BP_CursedChild_Visualizer_C` | `/Script/Engine.Actor` | yes (count 16) |
| `/Game/Sparta/Core/Player/Ability/Sidearm/CursedChild/GA_CursedChildAttack_Primary.GA_CursedChildAttack_Primary_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmRangedAttackBase.GA_SidearmRangedAttackBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Sidearm/CursedChild/GA_CursedChildLightHandler.GA_CursedChildLightHandler_C` | `/Game/Sparta/Core/Ability/GA_SpartaBase.GA_SpartaBase_C` | yes (count 17) |
| `/Game/Sparta/Core/Player/Ability/Sidearm/CursedChild/GA_CursedChildPrimaryActivation.GA_CursedChildPrimaryActivation_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmActivationBase.GA_SidearmActivationBase_C` | yes (count 9) |
| `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmActivationBase.GA_SidearmActivationBase_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 13) |
| `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmRangedAttackBase.GA_SidearmRangedAttackBase_C` | `/Game/Sparta/Core/Ability/GA_SpartaBase.GA_SpartaBase_C` | yes (count 14) |
| `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmRangedBurstAttackBase.GA_SidearmRangedBurstAttackBase_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmRangedAttackBase.GA_SidearmRangedAttackBase_C` | yes (count 1) |
| `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmRangedChargedAttackBase.GA_SidearmRangedChargedAttackBase_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmRangedAttackBase.GA_SidearmRangedAttackBase_C` | yes (count 16) |
| `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmSecondaryActivation.GA_SidearmSecondaryActivation_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmActivationBase.GA_SidearmActivationBase_C` | yes (count 1) |
| `/Game/Sparta/Core/Player/Ability/Sidearm/GA_Sidearm_Draw.GA_Sidearm_Draw_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | yes (count 1) |
| `/Game/Sparta/Core/Player/Ability/Sidearm/GA_Sidearm_Stow.GA_Sidearm_Stow_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | yes (count 1) |
| `/Game/Sparta/Core/Player/Ability/Sidearm/MachineGun/GA_MachineGunAttack_Primary.GA_MachineGunAttack_Primary_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmRangedBurstAttackBase.GA_SidearmRangedBurstAttackBase_C` | yes (count 5) |
| `/Game/Sparta/Core/Player/Ability/Sidearm/MachineGun/GA_MachineGunAttack_Primary_InfiniteAmmo.GA_MachineGunAttack_Primary_InfiniteAmmo_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/MachineGun/GA_MachineGunAttack_Primary.GA_MachineGunAttack_Primary_C` | none |
| `/Game/Sparta/Core/Player/Ability/Sidearm/MachineGun/GA_MachineGunPrimaryActivation.GA_MachineGunPrimaryActivation_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmActivationBase.GA_SidearmActivationBase_C` | yes (count 3) |
| `/Game/Sparta/Core/Player/Ability/Sidearm/NailShotgun/GA_NailShotgunAttack_Primary.GA_NailShotgunAttack_Primary_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmRangedAttackBase.GA_SidearmRangedAttackBase_C` | yes (count 1) |
| `/Game/Sparta/Core/Player/Ability/Sidearm/NailShotgun/GA_NailShotgunPrimaryActivation.GA_NailShotgunPrimaryActivation_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmActivationBase.GA_SidearmActivationBase_C` | yes (count 10) |
| `/Game/Sparta/Core/Player/Ability/Sidearm/ParasiteGun/GA_ParasiteGunAttack_Primary.GA_ParasiteGunAttack_Primary_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmRangedBurstAttackBase.GA_SidearmRangedBurstAttackBase_C` | yes (count 1) |
| `/Game/Sparta/Core/Player/Ability/Sidearm/ParasiteGun/GA_ParasiteGunPrimaryActivation.GA_ParasiteGunPrimaryActivation_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmActivationBase.GA_SidearmActivationBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Sidearm/SimpleLute/GA_SimpleLuteAttack_Primary.GA_SimpleLuteAttack_Primary_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmRangedAttackBase.GA_SidearmRangedAttackBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Sidearm/SimpleLute/GA_SimpleLuteMusicHandler.GA_SimpleLuteMusicHandler_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 35) |
| `/Game/Sparta/Core/Player/Ability/Sidearm/SimpleLute/GA_SimpleLutePrimaryActivation.GA_SimpleLutePrimaryActivation_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmActivationBase.GA_SidearmActivationBase_C` | yes (count 2) |
| `/Game/Sparta/Core/Player/Ability/Sidearm/SimpleLute/GA_SimpleLute_SitDown.GA_SimpleLute_SitDown_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Sidearm/SimpleLute/GA_SimpleLute_SitPlay.GA_SimpleLute_SitPlay_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Sidearm/SimpleLute/GA_SimpleLute_StandUp.GA_SimpleLute_StandUp_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | yes (count 1) |
| `/Game/Sparta/Core/Player/Ability/Sidearm/SimpleLute/GE_Lute_Play_Default.GE_Lute_Play_Default_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Player/Ability/Sidearm/SimpleLute/GE_Lute_Play_Duet.GE_Lute_Play_Duet_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Player/Ability/Sidearm/SimpleLute/GE_Lute_Play_SitDown.GE_Lute_Play_SitDown_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Player/Ability/Sidearm/Trebuchaxe/GA_TrebuchaxeAttack_Primary.GA_TrebuchaxeAttack_Primary_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmRangedChargedAttackBase.GA_SidearmRangedChargedAttackBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Sidearm/Trebuchaxe/GA_TrebuchaxeAttack_Secondary.GA_TrebuchaxeAttack_Secondary_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/Trebuchaxe/GA_TrebuchaxeAttack_Primary.GA_TrebuchaxeAttack_Primary_C` | none |
| `/Game/Sparta/Core/Player/Ability/Sidearm/Trebuchaxe/GA_TrebuchaxeAttack_Secondary_QuickShooting.GA_TrebuchaxeAttack_Secondary_QuickShooting_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/Trebuchaxe/GA_TrebuchaxeAttack_Secondary.GA_TrebuchaxeAttack_Secondary_C` | yes (count 1) |
| `/Game/Sparta/Core/Player/Ability/Sidearm/Trebuchaxe/GA_TrebuchaxeAttack_Secondary_Tiltshot.GA_TrebuchaxeAttack_Secondary_Tiltshot_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/Trebuchaxe/GA_TrebuchaxeAttack_Secondary.GA_TrebuchaxeAttack_Secondary_C` | yes (count 1) |
| `/Game/Sparta/Core/Player/Ability/Sidearm/Trebuchaxe/GA_TrebuchaxePrimaryActivation.GA_TrebuchaxePrimaryActivation_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmActivationBase.GA_SidearmActivationBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Sidearm/Trebuchaxe/GA_TrebuchaxeSecondaryActivation.GA_TrebuchaxeSecondaryActivation_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmSecondaryActivation.GA_SidearmSecondaryActivation_C` | none |

Evidence -- sidearm base classes:
```
[000001F617F1D1B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmRangedAttackBase.GA_SidearmRangedAttackBase_C [n: 150A02] [c: 00007FF46EEC3D38] [or: 000001F4D1C18A30] [sps: 000001F615243160]
[000001F61D752DD0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Sidearm/GA_Sidearm_Draw.GA_Sidearm_Draw_C [n: 2D287E] [c: 00007FF46EEC3D38] [or: 000001F4D1C19520] [sps: 000001F615242DD0]
[000001F61D752A40] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Sidearm/GA_Sidearm_Stow.GA_Sidearm_Stow_C [n: E0496] [c: 00007FF46EEC3D38] [or: 000001F4D1C19B10] [sps: 000001F615242DD0]
[000001F7FC0DAA80] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmRangedChargedAttackBase.GA_SidearmRangedChargedAttackBase_C [n: 46FAF5] [c: 00007FF46EEC3D38] [or: 000001F80A6E8DA0] [sps: 000001F617F1D1B0]
[000001F80CB71150] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmActivationBase.GA_SidearmActivationBase_C [n: 353C9D] [c: 00007FF46EEC3D38] [or: 000001F80A6E99D0] [sps: 000001F4DC018E00]
[000001F81A290DC0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmSecondaryActivation.GA_SidearmSecondaryActivation_C [n: 35CC40] [c: 00007FF46EEC3D38] [or: 000001F81A28C220] [sps: 000001F80CB71150]
[000001F8D7694A50] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmRangedBurstAttackBase.GA_SidearmRangedBurstAttackBase_C [n: 89034] [c: 00007FF46EEC3D38] [or: 000001F8D76040C0] [sps: 000001F617F1D1B0]
```

---

## 10. Shell abilities -- `/Game/Sparta/Core/Characters/Player/`

57 `GA_*` classes. Three kinds live here:

1. **Shell-ability cores** (`parent GA_ShellAbilityBase_C`, which is a `GA_CharacterAbilityBase_C`):
   `GA_Gragu_Ability`, `GA_Harros_Ability`, `GA_KnightLady_Ability`, `GA_ShoulderBash`,
   `GA_SplendidArmor_Default`, `GA_ThornAttack`, `GA_AstralArmy_Action`, `GA_LingeringShadow`,
   `GA_Smert_FightStanceHandler`. These are the per-shell "special"/shell-skill abilities bound to the
   shell input (sec.8.3 `GA_Action_Ability_Shell_*` / `GA_Action_SpecialAbility_Shell_*`).
2. **Shell helpers / trackers** (`parent GA_SpartaAbility_C` or `GA_SpartaBase_C`):
   `GA_Smert_FistTracker`, `GA_Smert_KickTracker`, `GA_Smert_GroundPunchTracker`, `GA_StoneStun_Handler`,
   `GA_Eredrim_Passive`, `GA_Gragu_Passive`, `GA_ShellSummon_*`, `GA_Shell_AI_BossTracker`,
   `GA_DarkForm_Robe_Handler`, `GA_AstralClones_Action`, `GA_EggmanLaser`.
3. **Weapon/attack abilities that belong to a shell rather than a weapon**: `GA_Player_Attack_Light_Smert`,
   `GA_Player_Attack_Heavy_Smert`, `GA_Player_Attack_Smert_Kick_B1..B3`, `GA_Player_Attack_Smert_Punch_A1..A3`,
   `GA_Player_Attack_Smert_Punch_Ground`, `GA_Player_Attack_Smert_Running_A/B`,
   `GA_Smert_AttackSelector_Light/Heavy`, `GA_Gragu_Attack`, `GA_ShoulderBash_Attack`,
   `GA_Attack_KnightLady_FollowUp`, `GA_LingeringShadow_Attack`, `GA_ThornAttack_AI`,
   `GA_AstralGenessa_AttackBase` + `_MeleeAttack` + `_CopyAnyAttack`, `GA_ShellSummon_Dash/Death/Spawn/Despawn`.

`GA_CharacterAbilityBase_C` (the parent of the shell cores) supplies the timer/cooldown plumbing:

| Property | Type | Offset |
|---|---|---|
| `ActivationQuery` | `GameplayTagQuery` | `0x518` |
| `CooldownEventTag`, `StartEventTag`, `EndEventTag` | `GameplayTag` | `0x560`, `0x568`, `0x570` |
| `CustomCooldown` | `DoubleProperty` | `0x578` |
| `bUsesDynamicTimer` | `BoolProperty` | `0x580` |
| `CharacterAbilityTimerID` | `StrProperty` | `0x588` |
| `duration` | `FloatProperty` | `0x598` |
| `AbilityDynamicTimer` | `StructProperty of Sparta.SpartaTimerHandle` | `0x59C` |

`GA_ShellAbilityBase_C` adds: `ShellAbilityTimer: TimerHandle` `0x5A8`, `HitReactionEventTag: GameplayTag`
`0x5B0`, `SeveredWaitTask: AbilityTask_WaitGameplayEvent` `0x5B8`, `ForceCancelTriggersOnce: Bool`
`0x5C0`, `RespondToReactionTypes: GameplayTagContainer` `0x5C8`.

GUESS: a shell ability's real numbers come from `UShellAbilityAttributeSet` (sec.2.4:
`PrimaryEnergyCost`, `SecondaryEnergyCost`, `Cooldown`, `duration`, `Damage`...), i.e. the ability class
is the behaviour and the attribute set is the data. A sync layer should mirror the attribute set and
broadcast ability activation, not replicate per-shell ability internals.

### 10.1 Enumeration

| Class | Parent | Own props |
|---|---|---|
| `/Game/Sparta/Core/Characters/Player/AstralGenessa/Abilities/GA_AstralGenessa_AttackBase.GA_AstralGenessa_AttackBase_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 4) |
| `/Game/Sparta/Core/Characters/Player/AstralGenessa/Abilities/GA_AstralGenessa_CopyAnyAttack.GA_AstralGenessa_CopyAnyAttack_C` | `/Game/Sparta/Core/Characters/Player/AstralGenessa/Abilities/GA_AstralGenessa_AttackBase.GA_AstralGenessa_AttackBase_C` | yes (count 4) |
| `/Game/Sparta/Core/Characters/Player/AstralGenessa/Abilities/GA_AstralGenessa_MeleeAttack.GA_AstralGenessa_MeleeAttack_C` | `/Game/Sparta/Core/Characters/Player/AstralGenessa/Abilities/GA_AstralGenessa_AttackBase.GA_AstralGenessa_AttackBase_C` | yes (count 5) |
| `/Game/Sparta/Core/Characters/Player/AstralGenessa/BP_AIController_AstralGenessa.BP_AIController_AstralGenessa_C` | `/Game/Sparta/Core/AI/BP_AIController.BP_AIController_C` | yes (count 3) |
| `/Game/Sparta/Core/Characters/Player/AstralGenessa/BP_Shell_Genessa_AstralBase.BP_Shell_Genessa_AstralBase_C` | `/Game/Sparta/Core/Characters/Player/Genessa/BP_Shell_Genessa.BP_Shell_Genessa_C` | yes (count 7) |
| `/Game/Sparta/Core/Characters/Player/AstralGenessa/BP_Shell_Genessa_AstralClone.BP_Shell_Genessa_AstralClone_C` | `/Game/Sparta/Core/Characters/Player/AstralGenessa/BP_Shell_Genessa_AstralBase.BP_Shell_Genessa_AstralBase_C` | yes (count 4) |
| `/Game/Sparta/Core/Characters/Player/AstralGenessa/BP_Shell_Genessa_AstralClone_Single.BP_Shell_Genessa_AstralClone_Single_C` | `/Game/Sparta/Core/Characters/Player/AstralGenessa/BP_Shell_Genessa_AstralClone.BP_Shell_Genessa_AstralClone_C` | yes (count 3) |
| `/Game/Sparta/Core/Characters/Player/AstralGenessa/BP_Shell_Genessa_AstralCopy.BP_Shell_Genessa_AstralCopy_C` | `/Game/Sparta/Core/Characters/Player/AstralGenessa/BP_Shell_Genessa_AstralBase.BP_Shell_Genessa_AstralBase_C` | yes (count 4) |
| `/Game/Sparta/Core/Characters/Player/AstralGenessa/Effects/CCC_Genessa_Astral_DamageScale.CCC_Genessa_Astral_DamageScale_C` | `/Script/Sparta.SpartaModMagnitudeCalculation` | none |
| `/Game/Sparta/Core/Characters/Player/AstralGenessa/Effects/GE_Genessa_AstralClone.GE_Genessa_AstralClone_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Characters/Player/AstralGenessa/Effects/GE_Genessa_AstralClone_Melee_BaseDamage.GE_Genessa_AstralClone_Melee_BaseDamage_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Characters/Player/AstralGenessa/Effects/GE_Genessa_AstralClone_Ranged_BaseDamage.GE_Genessa_AstralClone_Ranged_BaseDamage_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Characters/Player/AstralGenessa/Effects/GE_Genessa_AstralCopy.GE_Genessa_AstralCopy_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Characters/Player/AstralGenessa/Effects/GE_Genessa_AstralCopy_BaseDamage.GE_Genessa_AstralCopy_BaseDamage_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Characters/Player/AstralGenessa/Effects/GE_Genessa_Astral_DamageScale.GE_Genessa_Astral_DamageScale_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Characters/Player/Common/ABP_Shell_Default.ABP_Shell_Default_C` | `/Game/Sparta/Characters/Enemies/_Shared/ABP_AI_Humanoid.ABP_AI_Humanoid_C` | yes (count 1) |
| `/Game/Sparta/Core/Characters/Player/Common/Abilities/ActiveBlock/GA_ActiveBlock.GA_ActiveBlock_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 22) |
| `/Game/Sparta/Core/Characters/Player/Common/Abilities/GA_Dash_Shell_AI.GA_Dash_Shell_AI_C` | `/Game/Sparta/Core/Player/Ability/Dash/GA_DashBase.GA_DashBase_C` | none |
| `/Game/Sparta/Core/Characters/Player/Common/Abilities/GA_Player_PrimaryWeapon_Draw.GA_Player_PrimaryWeapon_Draw_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | yes (count 1) |
| `/Game/Sparta/Core/Characters/Player/Common/Abilities/GA_Player_PrimaryWeapon_Stow.GA_Player_PrimaryWeapon_Stow_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | yes (count 4) |
| `/Game/Sparta/Core/Characters/Player/Common/Abilities/GA_Shell_AI_BossTracker.GA_Shell_AI_BossTracker_C` | `/Game/Sparta/Core/Ability/GA_SpartaBase.GA_SpartaBase_C` | yes (count 9) |
| `/Game/Sparta/Core/Characters/Player/Common/Abilities/StoneForm/BP_Harden_PP.BP_Harden_PP_C` | `/Script/Engine.Actor` | yes (count 33) |
| `/Game/Sparta/Core/Characters/Player/Common/Abilities/StoneForm/GA_Harden_Original.GA_Harden_Original_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 37) |
| `/Game/Sparta/Core/Characters/Player/Common/Abilities/StoneForm/GE_Harden_Active.GE_Harden_Active_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Characters/Player/Common/Abilities/StoneForm/GE_Harden_Perfect.GE_Harden_Perfect_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Characters/Player/Common/BP_DarkForm_Spawnable.BP_DarkForm_Spawnable_C` | `/Script/Sparta.SpartaCharacter` | yes (count 7) |
| `/Game/Sparta/Core/Characters/Player/Common/BP_Decoy_Bait.BP_Decoy_Bait_C` | `/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Decoy.BP_Shell_Decoy_C` | yes (count 3) |
| `/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Base.BP_Shell_Base_C` | `/Game/Sparta/Core/Player/BP_PlayerCharacter.BP_PlayerCharacter_C` | yes (count 18) |
| `/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Decor.BP_Shell_Decor_C` | `/Script/Engine.Actor` | yes (count 15) |
| `/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Decoy.BP_Shell_Decoy_C` | `/Script/Sparta.SpartaCharacter` | yes (count 3) |
| `/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Dummy_Grabbed.BP_Shell_Dummy_Grabbed_C` | `/Script/Sparta.SpartaCharacter` | yes (count 2) |
| `/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Spawnable.BP_Shell_Spawnable_C` | `/Script/Sparta.SpartaCharacter` | yes (count 8) |
| `/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Spawnable_Ghost.BP_Shell_Spawnable_Ghost_C` | `/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Spawnable.BP_Shell_Spawnable_C` | yes (count 4) |
| `/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Spawnable_Traversal.BP_Shell_Spawnable_Traversal_C` | `/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Spawnable.BP_Shell_Spawnable_C` | yes (count 4) |
| `/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Summon.BP_Shell_Summon_C` | `/Script/Sparta.SpartaAICharacter` | yes (count 41) |
| `/Game/Sparta/Core/Characters/Player/Common/GE_ShellAICharacter.GE_ShellAICharacter_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Characters/Player/CorruptedGenessa/Abilities/GA_AstralClones_Action.GA_AstralClones_Action_C` | `/Game/Sparta/Core/Player/Ability/GA_DarkformAbilityBase.GA_DarkformAbilityBase_C` | yes (count 15) |
| `/Game/Sparta/Core/Characters/Player/CorruptedGenessa/BP_Shell_CorruptedGenessa.BP_Shell_CorruptedGenessa_C` | `/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Base.BP_Shell_Base_C` | yes (count 4) |
| `/Game/Sparta/Core/Characters/Player/Eggman/Abilities/GA_EggmanLaser.GA_EggmanLaser_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 3) |
| `/Game/Sparta/Core/Characters/Player/Eggman/BP_Darkform_Eggman.BP_Darkform_Eggman_C` | `/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Base.BP_Shell_Base_C` | yes (count 10) |
| `/Game/Sparta/Core/Characters/Player/Eredrim/ABP_Shell_Eredrim.ABP_Shell_Eredrim_C` | `/Game/Sparta/Characters/Enemies/_Shared/ABP_AI_Humanoid.ABP_AI_Humanoid_C` | yes (count 1) |
| `/Game/Sparta/Core/Characters/Player/Eredrim/Abilities/GA_Eredrim_Diapason.GA_Eredrim_Diapason_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | yes (count 8) |
| `/Game/Sparta/Core/Characters/Player/Eredrim/Abilities/GA_Eredrim_Passive.GA_Eredrim_Passive_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 1) |
| `/Game/Sparta/Core/Characters/Player/Eredrim/Abilities/GA_ShoulderBash.GA_ShoulderBash_C` | `/Game/Sparta/Core/Player/Ability/GA_ShellAbilityBase.GA_ShellAbilityBase_C` | yes (count 13) |
| `/Game/Sparta/Core/Characters/Player/Eredrim/Abilities/GA_ShoulderBash_Attack.GA_ShoulderBash_Attack_C` | `/Game/Sparta/Core/Ability/GA_AttackBase_Melee.GA_AttackBase_Melee_C` | yes (count 7) |
| `/Game/Sparta/Core/Characters/Player/Eredrim/BP_Shell_Eredrim.BP_Shell_Eredrim_C` | `/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Base.BP_Shell_Base_C` | yes (count 5) |
| `/Game/Sparta/Core/Characters/Player/Genessa/ABP_Shell_Genessa.ABP_Shell_Genessa_C` | `/Game/Sparta/Characters/Enemies/_Shared/ABP_AI_Humanoid.ABP_AI_Humanoid_C` | yes (count 1) |
| `/Game/Sparta/Core/Characters/Player/Genessa/Abilities/GA_AstralArmy_Action.GA_AstralArmy_Action_C` | `/Game/Sparta/Core/Player/Ability/GA_ShellAbilityBase.GA_ShellAbilityBase_C` | yes (count 5) |
| `/Game/Sparta/Core/Characters/Player/Genessa/BP_Shell_Genessa.BP_Shell_Genessa_C` | `/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Base.BP_Shell_Base_C` | yes (count 3) |
| `/Game/Sparta/Core/Characters/Player/Gragu/ABP_Shell_Gragu.ABP_Shell_Gragu_C` | `/Game/Sparta/Characters/Enemies/_Shared/ABP_AI_Humanoid.ABP_AI_Humanoid_C` | yes (count 1) |
| `/Game/Sparta/Core/Characters/Player/Gragu/Abilities/GA_Gragu_Ability.GA_Gragu_Ability_C` | `/Game/Sparta/Core/Player/Ability/GA_ShellAbilityBase.GA_ShellAbilityBase_C` | yes (count 7) |
| `/Game/Sparta/Core/Characters/Player/Gragu/Abilities/GA_Gragu_Attack.GA_Gragu_Attack_C` | `/Game/Sparta/Core/Ability/GA_AttackBase_Melee.GA_AttackBase_Melee_C` | yes (count 4) |
| `/Game/Sparta/Core/Characters/Player/Gragu/Abilities/GA_Gragu_Passive.GA_Gragu_Passive_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 4) |
| `/Game/Sparta/Core/Characters/Player/Gragu/Abilities/GE_Gragu_Heal.GE_Gragu_Heal_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Characters/Player/Gragu/Abilities/GE_Gragu_ModifyCurrentMeter.GE_Gragu_ModifyCurrentMeter_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Characters/Player/Gragu/BP_Gragu_Helmet.BP_Gragu_Helmet_C` | `/Game/Sparta/Items/Pickups/BP_Attachable_Item_Mask.BP_Attachable_Item_Mask_C` | yes (count 6) |
| `/Game/Sparta/Core/Characters/Player/Gragu/BP_Shell_Gragu.BP_Shell_Gragu_C` | `/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Base.BP_Shell_Base_C` | yes (count 6) |
| `/Game/Sparta/Core/Characters/Player/Gragu/BTT_GraguAbility.BTT_GraguAbility_C` | `/Game/Sparta/Core/AI/Task/BTTask_ShellAbility.BTTask_ShellAbility_C` | yes (count 1) |
| `/Game/Sparta/Core/Characters/Player/Harros/Abilities/GA_Harros_Ability.GA_Harros_Ability_C` | `/Game/Sparta/Core/Player/Ability/GA_ShellAbilityBase.GA_ShellAbilityBase_C` | yes (count 6) |
| `/Game/Sparta/Core/Characters/Player/Harros/Abilities/GA_StoneStun_Handler.GA_StoneStun_Handler_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 12) |
| `/Game/Sparta/Core/Characters/Player/Harros/Abilities/GE_StoneStun_Stack.GE_StoneStun_Stack_C` | `/Game/Sparta/Core/Effects/StatusEffects/GE_SpartaStatusEffectBase.GE_SpartaStatusEffectBase_C` | none |
| `/Game/Sparta/Core/Characters/Player/Harros/BP_Shell_Harros.BP_Shell_Harros_C` | `/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Base.BP_Shell_Base_C` | yes (count 3) |
| `/Game/Sparta/Core/Characters/Player/KnightLady/ABP_Shell_KnightLady.ABP_Shell_KnightLady_C` | `/Game/Sparta/Characters/Enemies/_Shared/ABP_AI_Humanoid.ABP_AI_Humanoid_C` | yes (count 1) |
| `/Game/Sparta/Core/Characters/Player/KnightLady/Abilities/GA_Attack_KnightLady_FollowUp.GA_Attack_KnightLady_FollowUp_C` | `/Game/Sparta/Core/Ability/GA_AttackBase_Melee.GA_AttackBase_Melee_C` | yes (count 4) |
| `/Game/Sparta/Core/Characters/Player/KnightLady/Abilities/GA_KnightLady_Ability.GA_KnightLady_Ability_C` | `/Game/Sparta/Core/Player/Ability/GA_ShellAbilityBase.GA_ShellAbilityBase_C` | yes (count 31) |
| `/Game/Sparta/Core/Characters/Player/KnightLady/Abilities/GE_KnightLady_Passive.GE_KnightLady_Passive_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Characters/Player/KnightLady/Abilities/HPM_KnightLady.HPM_KnightLady_C` | `/Script/Sparta.HitPayloadModifier` | yes (count 9) |
| `/Game/Sparta/Core/Characters/Player/KnightLady/Abilities/WP_KnightLady_Hook_Left.WP_KnightLady_Hook_Left_C` | `/Game/Sparta/Core/Weapons/WP_WeaponBase_Static.WP_WeaponBase_Static_C` | yes (count 44) |
| `/Game/Sparta/Core/Characters/Player/KnightLady/BP_Shell_KnightLady.BP_Shell_KnightLady_C` | `/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Base.BP_Shell_Base_C` | yes (count 3) |
| `/Game/Sparta/Core/Characters/Player/Necrophage/ABP_Shell_Necrophage.ABP_Shell_Necrophage_C` | `/Game/Sparta/Characters/Enemies/_Shared/ABP_AI_Humanoid.ABP_AI_Humanoid_C` | yes (count 1) |
| `/Game/Sparta/Core/Characters/Player/Necrophage/Abilities/BP_SplendidArmorHandler.BP_SplendidArmorHandler_C` | `/Script/Engine.Actor` | yes (count 18) |
| `/Game/Sparta/Core/Characters/Player/Necrophage/Abilities/GA_SplendidArmor_Cooldown.GA_SplendidArmor_Cooldown_C` | `/Game/Sparta/Core/Player/Ability/GA_CharacterAbilityBase.GA_CharacterAbilityBase_C` | yes (count 12) |
| `/Game/Sparta/Core/Characters/Player/Necrophage/Abilities/GA_SplendidArmor_Default.GA_SplendidArmor_Default_C` | `/Game/Sparta/Core/Player/Ability/GA_ShellAbilityBase.GA_ShellAbilityBase_C` | yes (count 6) |
| `/Game/Sparta/Core/Characters/Player/Necrophage/BP_Shell_Necrophage.BP_Shell_Necrophage_C` | `/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Base.BP_Shell_Base_C` | yes (count 3) |
| `/Game/Sparta/Core/Characters/Player/Necrophage/BP_Shell_Necrophage_NoMask.BP_Shell_Necrophage_NoMask_C` | `/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Base.BP_Shell_Base_C` | yes (count 3) |
| `/Game/Sparta/Core/Characters/Player/Necrophage/BP_Shell_Necrophage_Young.BP_Shell_Necrophage_Young_C` | `/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Base.BP_Shell_Base_C` | yes (count 5) |
| `/Game/Sparta/Core/Characters/Player/Necrophage/BP_Shell_Necrophage_YoungAdult.BP_Shell_Necrophage_YoungAdult_C` | `/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Base.BP_Shell_Base_C` | yes (count 4) |
| `/Game/Sparta/Core/Characters/Player/Necrophage/GE_SplendidArmor_Charged.GE_SplendidArmor_Charged_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Characters/Player/Necrophage/GE_SplendidArmor_Cooldown.GE_SplendidArmor_Cooldown_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Characters/Player/ShellSummon/Effects/GE_ShellSummon_CustomHealth.GE_ShellSummon_CustomHealth_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Characters/Player/ShellSummon/Effects/GE_ShellSummon_IncomingDamage.GE_ShellSummon_IncomingDamage_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Characters/Player/ShellSummon/Effects/GE_ShellSummon_OutgoingDamage.GE_ShellSummon_OutgoingDamage_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Characters/Player/ShellSummon/Effects/HPM_ShellSummon_IncomingDamage.HPM_ShellSummon_IncomingDamage_C` | `/Script/Sparta.HitPayloadModifier` | none |
| `/Game/Sparta/Core/Characters/Player/ShellSummon/Effects/HPM_ShellSummon_OutgoingDamage.HPM_ShellSummon_OutgoingDamage_C` | `/Script/Sparta.HitPayloadModifier` | none |
| `/Game/Sparta/Core/Characters/Player/ShellSummon/GA_ShellSummon_Dash.GA_ShellSummon_Dash_C` | `/Game/Sparta/Core/Player/Ability/Dash/GA_DashBase.GA_DashBase_C` | yes (count 1) |
| `/Game/Sparta/Core/Characters/Player/ShellSummon/GA_ShellSummon_Death.GA_ShellSummon_Death_C` | `/Game/Sparta/Core/Player/Ability/GA_Death_Player.GA_Death_Player_C` | none |
| `/Game/Sparta/Core/Characters/Player/ShellSummon/GA_ShellSummon_Despawn.GA_ShellSummon_Despawn_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | yes (count 1) |
| `/Game/Sparta/Core/Characters/Player/ShellSummon/GA_ShellSummon_Handler.GA_ShellSummon_Handler_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 3) |
| `/Game/Sparta/Core/Characters/Player/ShellSummon/GA_ShellSummon_InvisibilityHandler.GA_ShellSummon_InvisibilityHandler_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 4) |
| `/Game/Sparta/Core/Characters/Player/ShellSummon/GA_ShellSummon_Spawn.GA_ShellSummon_Spawn_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Characters/Player/ShellSummon/GA_ShellSummon_Sprint.GA_ShellSummon_Sprint_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 4) |
| `/Game/Sparta/Core/Characters/Player/Smert/ABP_Shell_Smert.ABP_Shell_Smert_C` | `/Game/Sparta/Characters/Enemies/_Shared/ABP_AI_Humanoid.ABP_AI_Humanoid_C` | yes (count 1) |
| `/Game/Sparta/Core/Characters/Player/Smert/Abilities/BP_Smert_TimeStop_AI.BP_Smert_TimeStop_AI_C` | `/Script/Engine.Actor` | yes (count 12) |
| `/Game/Sparta/Core/Characters/Player/Smert/Abilities/CCC_Smert_DOT.CCC_Smert_DOT_C` | `/Script/GameplayAbilities.GameplayModMagnitudeCalculation` | yes (count 1) |
| `/Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Player_Attack_Heavy_Smert.GA_Player_Attack_Heavy_Smert_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | yes (count 4) |
| `/Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Player_Attack_Light_Smert.GA_Player_Attack_Light_Smert_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Player_Attack_Smert_Kick_B1.GA_Player_Attack_Smert_Kick_B1_C` | `/Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Player_Attack_Heavy_Smert.GA_Player_Attack_Heavy_Smert_C` | none |
| `/Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Player_Attack_Smert_Kick_B2.GA_Player_Attack_Smert_Kick_B2_C` | `/Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Player_Attack_Heavy_Smert.GA_Player_Attack_Heavy_Smert_C` | none |
| `/Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Player_Attack_Smert_Kick_B3.GA_Player_Attack_Smert_Kick_B3_C` | `/Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Player_Attack_Heavy_Smert.GA_Player_Attack_Heavy_Smert_C` | none |
| `/Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Player_Attack_Smert_Punch_A1.GA_Player_Attack_Smert_Punch_A1_C` | `/Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Player_Attack_Light_Smert.GA_Player_Attack_Light_Smert_C` | none |
| `/Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Player_Attack_Smert_Punch_A2.GA_Player_Attack_Smert_Punch_A2_C` | `/Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Player_Attack_Light_Smert.GA_Player_Attack_Light_Smert_C` | none |
| `/Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Player_Attack_Smert_Punch_A3.GA_Player_Attack_Smert_Punch_A3_C` | `/Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Player_Attack_Light_Smert.GA_Player_Attack_Light_Smert_C` | none |
| `/Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Player_Attack_Smert_Punch_Ground.GA_Player_Attack_Smert_Punch_Ground_C` | `/Game/Sparta/Core/Ability/GA_AttackBase_Melee.GA_AttackBase_Melee_C` | yes (count 2) |
| `/Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Player_Attack_Smert_Running_A.GA_Player_Attack_Smert_Running_A_C` | `/Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Player_Attack_Light_Smert.GA_Player_Attack_Light_Smert_C` | none |
| `/Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Player_Attack_Smert_Running_B.GA_Player_Attack_Smert_Running_B_C` | `/Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Player_Attack_Heavy_Smert.GA_Player_Attack_Heavy_Smert_C` | none |
| `/Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Smert_AttackSelector_Heavy.GA_Smert_AttackSelector_Heavy_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Heavy.GA_Player_AttackSelector_Heavy_C` | none |
| `/Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Smert_AttackSelector_Light.GA_Smert_AttackSelector_Light_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Light.GA_Player_AttackSelector_Light_C` | none |
| `/Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Smert_Faith.GA_Smert_Faith_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 3) |
| `/Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Smert_FightStanceHandler.GA_Smert_FightStanceHandler_C` | `/Game/Sparta/Core/Player/Ability/GA_ShellAbilityBase.GA_ShellAbilityBase_C` | yes (count 40) |
| `/Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Smert_FistTracker.GA_Smert_FistTracker_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 1) |
| `/Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Smert_GroundPunchTracker.GA_Smert_GroundPunchTracker_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 5) |
| `/Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Smert_KickTracker.GA_Smert_KickTracker_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 4) |
| `/Game/Sparta/Core/Characters/Player/Smert/Abilities/GE_Smert_DOT.GE_Smert_DOT_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Characters/Player/Smert/Abilities/GE_Smert_SelfDamage.GE_Smert_SelfDamage_C` | `/Game/Sparta/Core/Effects/GE_DamageBase.GE_DamageBase_C` | none |
| `/Game/Sparta/Core/Characters/Player/Smert/Abilities/GE_Smert_TimeStop_Active.GE_Smert_TimeStop_Active_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Characters/Player/Smert/BP_Shell_Smert.BP_Shell_Smert_C` | `/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Base.BP_Shell_Base_C` | yes (count 4) |
| `/Game/Sparta/Core/Characters/Player/Smert/GE_FightStance_Active.GE_FightStance_Active_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Characters/Player/Solomon/BP_Shell_Solomon.BP_Shell_Solomon_C` | `/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Base.BP_Shell_Base_C` | yes (count 3) |
| `/Game/Sparta/Core/Characters/Player/StrongOne/Abilities/GA_DarkForm_Robe_Handler.GA_DarkForm_Robe_Handler_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Characters/Player/StrongOne/BP_Darkform_StrongOne.BP_Darkform_StrongOne_C` | `/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Base.BP_Shell_Base_C` | yes (count 10) |
| `/Game/Sparta/Core/Characters/Player/StrongOne/BP_Darkform_StrongOne_Robe.BP_Darkform_StrongOne_Robe_C` | `/Game/Sparta/Core/Characters/Player/StrongOne/BP_Darkform_StrongOne.BP_Darkform_StrongOne_C` | yes (count 3) |
| `/Game/Sparta/Core/Characters/Player/StrongOne/BP_Darkform_StrongOne_Skeleton.BP_Darkform_StrongOne_Skeleton_C` | `/Game/Sparta/Core/Characters/Player/StrongOne/BP_Darkform_StrongOne.BP_Darkform_StrongOne_C` | yes (count 3) |
| `/Game/Sparta/Core/Characters/Player/TarredCorpse/BP_Darkform_TarredCorpse.BP_Darkform_TarredCorpse_C` | `/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Base.BP_Shell_Base_C` | yes (count 10) |
| `/Game/Sparta/Core/Characters/Player/ThornBoi/ABP_Shell_Thorn.ABP_Shell_Thorn_C` | `/Game/Sparta/Characters/Enemies/_Shared/ABP_AI_Humanoid.ABP_AI_Humanoid_C` | yes (count 1) |
| `/Game/Sparta/Core/Characters/Player/ThornBoi/Abilities/BPI_ThornArmorOwner.BPI_ThornArmorOwner_C` | `/Script/CoreUObject.Interface` | none |
| `/Game/Sparta/Core/Characters/Player/ThornBoi/Abilities/BP_ThornProjectile_Handler.BP_ThornProjectile_Handler_C` | `/Script/Engine.Actor` | yes (count 21) |
| `/Game/Sparta/Core/Characters/Player/ThornBoi/Abilities/GA_ThornAttack.GA_ThornAttack_C` | `/Game/Sparta/Core/Player/Ability/GA_ShellAbilityBase.GA_ShellAbilityBase_C` | yes (count 11) |
| `/Game/Sparta/Core/Characters/Player/ThornBoi/Abilities/GA_ThornAttack_AI.GA_ThornAttack_AI_C` | `/Game/Sparta/Core/Characters/Player/ThornBoi/Abilities/GA_ThornAttack.GA_ThornAttack_C` | none |
| `/Game/Sparta/Core/Characters/Player/ThornBoi/Abilities/GA_ThornHandler.GA_ThornHandler_C` | `/Game/Sparta/Core/Player/Ability/GA_CharacterAbilityBase.GA_CharacterAbilityBase_C` | yes (count 13) |
| `/Game/Sparta/Core/Characters/Player/ThornBoi/Abilities/GE_Thorn_Passive.GE_Thorn_Passive_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Characters/Player/ThornBoi/Abilities/HPM_Thorn_Passive.HPM_Thorn_Passive_C` | `/Script/Sparta.HitPayloadModifier` | yes (count 4) |
| `/Game/Sparta/Core/Characters/Player/ThornBoi/Abilities/WP_ThornArmor.WP_ThornArmor_C` | `/Game/Sparta/Core/Weapons/WP_WeaponBase.WP_WeaponBase_C` | yes (count 8) |
| `/Game/Sparta/Core/Characters/Player/ThornBoi/BP_Shell_Thorn.BP_Shell_Thorn_C` | `/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Base.BP_Shell_Base_C` | yes (count 5) |
| `/Game/Sparta/Core/Characters/Player/ThornBoi/BP_Shell_Thorn_Memories.BP_Shell_Thorn_Memories_C` | `/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Base.BP_Shell_Base_C` | yes (count 5) |
| `/Game/Sparta/Core/Characters/Player/Tiel/ABP_Shell_Tiel.ABP_Shell_Tiel_C` | `/Game/Sparta/Characters/Enemies/_Shared/ABP_AI_Humanoid.ABP_AI_Humanoid_C` | yes (count 1) |
| `/Game/Sparta/Core/Characters/Player/Tiel/Abilities/GA_LingeringShadow.GA_LingeringShadow_C` | `/Game/Sparta/Core/Player/Ability/GA_ShellAbilityBase.GA_ShellAbilityBase_C` | yes (count 4) |
| `/Game/Sparta/Core/Characters/Player/Tiel/Abilities/GA_LingeringShadow_Attack.GA_LingeringShadow_Attack_C` | `/Game/Sparta/Core/Ability/GA_AttackBase_Melee.GA_AttackBase_Melee_C` | yes (count 2) |
| `/Game/Sparta/Core/Characters/Player/Tiel/Abilities/GA_LingeringShadow_Stow.GA_LingeringShadow_Stow_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | yes (count 1) |
| `/Game/Sparta/Core/Characters/Player/Tiel/BP_Shell_Tiel.BP_Shell_Tiel_C` | `/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Base.BP_Shell_Base_C` | yes (count 4) |
| `/Game/Sparta/Core/Characters/Player/Tiel/BP_Shell_Tiel_ShadowClone.BP_Shell_Tiel_ShadowClone_C` | `/Script/Sparta.SpartaAICharacter` | yes (count 41) |
| `/Game/Sparta/Core/Characters/Player/Tiel/BP_Shell_Tiel_WithFace.BP_Shell_Tiel_WithFace_C` | `/Game/Sparta/Core/Characters/Player/Tiel/BP_Shell_Tiel.BP_Shell_Tiel_C` | yes (count 3) |
| `/Game/Sparta/Core/Characters/Player/Tiel/BTT_TielAbility.BTT_TielAbility_C` | `/Game/Sparta/Core/AI/Task/BTTask_ShellAbility.BTTask_ShellAbility_C` | yes (count 1) |

Evidence - shell ability class rows:
```
[000001F4DC01EE30] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/ShellSummon/GA_ShellSummon_InvisibilityHandler.GA_ShellSummon_InvisibilityHandler_C [n: 17EE46] [c: 00007FF46EEC3D38] [or: 000001F4D0156C80] [sps: 000001F4DC018E00]
[000001F4DCBAD540] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/ShellSummon/GA_ShellSummon_Sprint.GA_ShellSummon_Sprint_C [n: 46640E] [c: 00007FF46EEC3D38] [or: 000001F4D33C86C0] [sps: 000001F4DC018E00]
[000001F4DCBAFC70] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/ShellSummon/GA_ShellSummon_Handler.GA_ShellSummon_Handler_C [n: 2D667C] [c: 00007FF46EEC3D38] [or: 000001F4CEEDF240] [sps: 000001F4DC018E00]
[000001F617BE9190] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Common/Abilities/StoneForm/GA_Harden_Original.GA_Harden_Original_C [n: 2B3CAC] [c: 00007FF46EEC3D38] [or: 000001F4D1C1E0C0] [sps: 000001F4DC018E00]
[000001F61A5BF8E0] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Common/Abilities/GA_Dash_Shell_AI.GA_Dash_Shell_AI_C [n: 2C97D2] [c: 00007FF46EEC3D38] [or: 000001F4D1C19840] [sps: 000001F61A5BB8C0]
[000001F61C351C00] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/StrongOne/Abilities/GA_DarkForm_Robe_Handler.GA_DarkForm_Robe_Handler_C [n: 2610FC] [c: 00007FF46EEC3D38] [or: 000001F4CEED09C0] [sps: 000001F4DC018E00]
[000001F61D7534F0] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Common/Abilities/GA_Player_PrimaryWeapon_Stow.GA_Player_PrimaryWeapon_Stow_C [n: 4367A8] [c: 00007FF46EEC3D38] [or: 000001F4D1C192F0] [sps: 000001F615242DD0]
[000001F61D753160] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Common/Abilities/GA_Player_PrimaryWeapon_Draw.GA_Player_PrimaryWeapon_Draw_C [n: 231357] [c: 00007FF46EEC3D38] [or: 000001F4D1C19480] [sps: 000001F615242DD0]
[000001F61D75DFF0] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/ShellSummon/GA_ShellSummon_Dash.GA_ShellSummon_Dash_C [n: B4BD4] [c: 00007FF46EEC3D38] [or: 000001F4D1C196B0] [sps: 000001F61A5BB8C0]
[000001F61D75DC60] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/ShellSummon/GA_ShellSummon_Spawn.GA_ShellSummon_Spawn_C [n: 38C78B] [c: 00007FF46EEC3D38] [or: 000001F4D1C19ED0] [sps: 000001F615242DD0]
[000001F61FE15FB0] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/ShellSummon/GA_ShellSummon_Despawn.GA_ShellSummon_Despawn_C [n: 491DBE] [c: 00007FF46EEC3D38] [or: 000001F4D1C17AE0] [sps: 000001F615242DD0]
[000001F61FE15890] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Common/Abilities/GA_Shell_AI_BossTracker.GA_Shell_AI_BossTracker_C [n: 4367B8] [c: 00007FF46EEC3D38] [or: 000001F4D1C179A0] [sps: 000001F615243160]
[000001F61FE13160] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/ShellSummon/GA_ShellSummon_Death.GA_ShellSummon_Death_C [n: 41B940] [c: 00007FF46EEC3D38] [or: 000001F4D1C171D0] [sps: 000001F61D753C10]
[000001F76D89DC60] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Tiel/Abilities/GA_LingeringShadow_Stow.GA_LingeringShadow_Stow_C [n: 3C6ED3] [c: 00007FF46EEC3D38] [or: 000001F76386BB90] [sps: 000001F615242DD0]
[000001F76957EAA0] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Tiel/Abilities/GA_LingeringShadow.GA_LingeringShadow_C [n: 2EBAB2] [c: 00007FF46EEC3D38] [or: 000001F76386CA40] [sps: 000001F7695706A0]
[000001F76957D8D0] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Tiel/Abilities/GA_LingeringShadow_Attack.GA_LingeringShadow_Attack_C [n: 183749] [c: 00007FF46EEC3D38] [or: 000001F76386CCC0] [sps: 000001F4BBD2F550]
[000001F4C20EB8C0] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/ThornBoi/Abilities/GA_ThornAttack.GA_ThornAttack_C [n: 1956F5] [c: 00007FF46EEC3D38] [or: 000001F4C9366320] [sps: 000001F7695706A0]
[000001F4C20EB1A0] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/ThornBoi/Abilities/GA_ThornAttack_AI.GA_ThornAttack_AI_C [n: 3F7AE5] [c: 00007FF46EEC3D38] [or: 000001F4C936D530] [sps: 000001F4C20EB8C0]
[000001F4C20E9FD0] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/ThornBoi/Abilities/GA_ThornHandler.GA_ThornHandler_C [n: 3583B2] [c: 00007FF46EEC3D38] [or: 000001F4C936D670] [sps: 000001F76D897C30]
[000001F891DF7C30] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Necrophage/Abilities/GA_SplendidArmor_Cooldown.GA_SplendidArmor_Cooldown_C [n: 239A61] [c: 00007FF46EEC3D38] [or: 000001F88CA18300] [sps: 000001F76D897C30]
[000001F891DFFC70] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Necrophage/Abilities/GA_SplendidArmor_Default.GA_SplendidArmor_Default_C [n: 1547DB] [c: 00007FF46EEC3D38] [or: 000001F88CA18260] [sps: 000001F7695706A0]
[000001F8B0E24A50] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/AstralGenessa/Abilities/GA_AstralGenessa_AttackBase.GA_AstralGenessa_AttackBase_C [n: 43F355] [c: 00007FF46EEC3D38] [or: 000001F8B6CCA010] [sps: 000001F4DC018E00]
[000001F8B0E23160] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/AstralGenessa/Abilities/GA_AstralGenessa_CopyAnyAttack.GA_AstralGenessa_CopyAnyAttack_C [n: 28CE7F] [c: 00007FF46EEC3D38] [or: 000001F8B6CC9D90] [sps: 000001F8B0E24A50]
[000001F8B0E22A40] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/AstralGenessa/Abilities/GA_AstralGenessa_MeleeAttack.GA_AstralGenessa_MeleeAttack_C [n: CE59C] [c: 00007FF46EEC3D38] [or: 000001F8B6CCADD0] [sps: 000001F8B0E24A50]
[000001F8B0F17510] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Common/Abilities/ActiveBlock/GA_ActiveBlock.GA_ActiveBlock_C [n: 41B83F] [c: 00007FF46EEC3D38] [or: 000001F8B6CCD350] [sps: 000001F4DC018E00]
[000001F8B0F15170] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/CorruptedGenessa/Abilities/GA_AstralClones_Action.GA_AstralClones_Action_C [n: 187E8D] [c: 00007FF46EEC3D38] [or: 000001F8B6CCEC00] [sps: 000001F8A7F24330]
[000001F8B0F19190] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Eggman/Abilities/GA_EggmanLaser.GA_EggmanLaser_C [n: 28CE90] [c: 00007FF46EEC3D38] [or: 000001F8B6CCF510] [sps: 000001F4DC018E00]
[000001F8B0CAAE10] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Eredrim/Abilities/GA_Eredrim_Diapason.GA_Eredrim_Diapason_C [n: 2EFF43] [c: 00007FF46EEC3D38] [or: 000001F8B1022EF0] [sps: 000001F615242DD0]
[000001F8B0CABC50] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Eredrim/Abilities/GA_Eredrim_Passive.GA_Eredrim_Passive_C [n: 17EE02] [c: 00007FF46EEC3D38] [or: 000001F8B1022CC0] [sps: 000001F4DC018E00]
[000001F8B0CA7180] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Eredrim/Abilities/GA_ShoulderBash_Attack.GA_ShoulderBash_Attack_C [n: 3726F3] [c: 00007FF46EEC3D38] [or: 000001F8B10247F0] [sps: 000001F4BBD2F550]
[000001F8B0CA7510] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Eredrim/Abilities/GA_ShoulderBash.GA_ShoulderBash_C [n: 26E284] [c: 00007FF46EEC3D38] [or: 000001F8B1024110] [sps: 000001F7695706A0]
[000001F8B6CD9190] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Genessa/Abilities/GA_AstralArmy_Action.GA_AstralArmy_Action_C [n: 1F4FBF] [c: 00007FF46EEC3D38] [or: 000001F8B1025290] [sps: 000001F7695706A0]
[000001F8B6CDAA80] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Gragu/Abilities/GA_Gragu_Attack.GA_Gragu_Attack_C [n: D7225] [c: 00007FF46EEC3D38] [or: 000001F8B1028490] [sps: 000001F4BBD2F550]
[000001F8B6CDCA90] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Gragu/Abilities/GA_Gragu_Ability.GA_Gragu_Ability_C [n: DB65F] [c: 00007FF46EEC3D38] [or: 000001F8B1028440] [sps: 000001F7695706A0]
[000001F8B6CDA6F0] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Gragu/Abilities/GA_Gragu_Passive.GA_Gragu_Passive_C [n: 1DA814] [c: 00007FF46EEC3D38] [or: 000001F8B1028580] [sps: 000001F4DC018E00]
[000001F8A7F2EE30] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Harros/Abilities/GA_StoneStun_Handler.GA_StoneStun_Handler_C [n: 10B124] [c: 00007FF46EEC3D38] [or: 000001F8B102ACE0] [sps: 000001F4DC018E00]
[000001F8A7F29190] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Harros/Abilities/GA_Harros_Ability.GA_Harros_Ability_C [n: 2F476C] [c: 00007FF46EEC3D38] [or: 000001F8B102AD80] [sps: 000001F7695706A0]
[000001F8B5896DF0] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/KnightLady/Abilities/GA_Attack_KnightLady_FollowUp.GA_Attack_KnightLady_FollowUp_C [n: 3251A6] [c: 00007FF46EEC3D38] [or: 000001F8B102ECF0] [sps: 000001F4BBD2F550]
[000001F8B5895C20] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/KnightLady/Abilities/GA_KnightLady_Ability.GA_KnightLady_Ability_C [n: 1A6C3C] [c: 00007FF46EEC3D38] [or: 000001F8B102E840] [sps: 000001F7695706A0]
[000001F8B589E380] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Player_Attack_Heavy_Smert.GA_Player_Attack_Heavy_Smert_C [n: C656D] [c: 00007FF46EEC3D38] [or: 000001F8B5932630] [sps: 000001F4C16814E0]
[000001F8B589D8D0] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Player_Attack_Light_Smert.GA_Player_Attack_Light_Smert_C [n: 317E71] [c: 00007FF46EEC3D38] [or: 000001F8B5932770] [sps: 000001F4B1779520]
[000001F8B589A360] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Player_Attack_Smert_Punch_Ground.GA_Player_Attack_Smert_Punch_Ground_C [n: 450247] [c: 00007FF46EEC3D38] [or: 000001F8B5932220] [sps: 000001F4BBD2F550]
[000001F8B5899C40] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Player_Attack_Smert_Running_A.GA_Player_Attack_Smert_Running_A_C [n: 2F000E] [c: 00007FF46EEC3D38] [or: 000001F8B5932130] [sps: 000001F8B589D8D0]
[000001F8B5899520] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Player_Attack_Smert_Running_B.GA_Player_Attack_Smert_Running_B_C [n: F9A55] [c: 00007FF46EEC3D38] [or: 000001F8B59320E0] [sps: 000001F8B589E380]
[000001F8B5898E00] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Smert_AttackSelector_Heavy.GA_Smert_AttackSelector_Heavy_C [n: 3B93C9] [c: 00007FF46EEC3D38] [or: 000001F8B5932090] [sps: 000001F4C16846C0]
[000001F8B58986E0] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Smert_AttackSelector_Light.GA_Smert_AttackSelector_Light_C [n: 143A10] [c: 00007FF46EEC3D38] [or: 000001F8B5932040] [sps: 000001F4C1683FA0]
[000001F8B58978A0] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Smert_Faith.GA_Smert_Faith_C [n: 3ABB87] [c: 00007FF46EEC3D38] [or: 000001F8B5931FA0] [sps: 000001F4DC018E00]
[000001F8B5A53160] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Smert_FightStanceHandler.GA_Smert_FightStanceHandler_C [n: 31C53C] [c: 00007FF46EEC3D38] [or: 000001F8B59330D0] [sps: 000001F7695706A0]
[000001F8B589F550] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Smert_FistTracker.GA_Smert_FistTracker_C [n: 1D1DEC] [c: 00007FF46EEC3D38] [or: 000001F8B59334E0] [sps: 000001F4DC018E00]
[000001F8B5A5F550] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Smert_GroundPunchTracker.GA_Smert_GroundPunchTracker_C [n: 13F607] [c: 00007FF46EEC3D38] [or: 000001F8B5934660] [sps: 000001F4DC018E00]
[000001F8B7EE7180] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Smert/Abilities/GA_Smert_KickTracker.GA_Smert_KickTracker_C [n: 1CD8CA] [c: 00007FF46EEC3D38] [or: 000001F8B59333F0] [sps: 000001F4DC018E00]
```

### 10.2 Shell state abilities: severed / downed / revive / traversal

This is the most co-op-relevant sub-family in the whole report: losing the shell, being downed, reviving
and the shell-throw traversal all involve two actors.

| Class | Parent | Own props |
|---|---|---|
| `/Game/Sparta/Core/Player/Ability/ShellSevered/GA_ShellSeveredBase.GA_ShellSeveredBase_C` | `GA_SpartaAbility_C` | 7 |
| `.../ShellSevered/GA_ShellSevered_Genessa.GA_ShellSevered_Genessa_C` | `GA_ShellSeveredBase_C` | none |
| `.../ShellSevered/GA_ShellSevered_Thorn.GA_ShellSevered_Thorn_C` | `GA_ShellSeveredBase_C` | none |
| `.../ShellSevered/GA_ShellSeveredAnimationBase.GA_ShellSeveredAnimationBase_C` | `GA_PlayMontageBase_C` | 1 |
| `.../ShellSevered/GA_ShellSeveredAnimation_Genessa.GA_ShellSeveredAnimation_Genessa_C` | `GA_ShellSeveredAnimationBase_C` | 2 |
| `.../ShellSevered/GA_ShellSeveredAnimation_StrongOne.GA_ShellSeveredAnimation_StrongOne_C` | `GA_ShellSeveredAnimationBase_C` | 2 |
| `.../ShellSevered/GA_ShellSeveredAnimation_TarGolemFight.GA_ShellSeveredAnimation_TarGolemFight_C` | `GA_ShellSeveredAnimationBase_C` | none |
| `.../ShellSevered/GA_ShellSeveredAnimation_TarredCorpse.GA_ShellSeveredAnimation_TarredCorpse_C` | `GA_ShellSeveredAnimationBase_C` | 2 |
| `.../ShellDowned/GA_ShellDownedBase.GA_ShellDownedBase_C` | `GA_SpartaAbility_C` | 3 |
| `.../ShellRevive/GA_ShellReviveBase.GA_ShellReviveBase_C` | `GA_SpartaAbility_C` | 6 |
| `.../ShellRevive/GA_ShellRevive_Genessa.GA_ShellRevive_Genessa_C` | `GA_ShellReviveBase_C` | none |
| `.../ShellRevive/GA_ShellRevive_SavedByTheShell.GA_ShellRevive_SavedByTheShell_C` | `GA_ShellReviveBase_C` | none |
| `.../ShellRevive/GA_ShellReviveAction.GA_ShellReviveAction_C` | `GA_SpartaAbility_C` | 11 |
| `.../ShellTraversal/GA_ShellTraversalBase.GA_ShellTraversalBase_C` | `GA_SpartaAbility_C` | 7 |
| `.../ShellTraversal/GA_ShellTraversal_CorruptedGenessa.GA_ShellTraversal_CorruptedGenessa_C` | `GA_ShellTraversalBase_C` | none |
| `.../ShellTraversal/GA_ShellTraversal_Default.GA_ShellTraversal_Default_C` | `GA_ShellTraversalBase_C` | none |
| `.../ShellTraversal/GA_ShellTraversal_Genessa.GA_ShellTraversal_Genessa_C` | `GA_ShellTraversalBase_C` | none |
| `.../ShellTraversal/GA_ShellTraversal_StrongOne.GA_ShellTraversal_StrongOne_C` | `GA_ShellTraversalBase_C` | none |

`GA_ShellSeveredBase_C` (the "your shell was severed" state):

| Property | Type | Offset |
|---|---|---|
| `SpawnedShell` | `ObjectProperty of Engine.Actor` | `0x518` |
| `DarkFormID` | `GameplayTag` | `0x520` |
| `TestingCVar` | `StrProperty` | `0x528` |
| `ShellSpawnPreventionTags` | `GameplayTagContainer` | `0x538` |
| `bInPairedAnim` | `BoolProperty` | `0x558` |
| `bInstigatedByPairedAnim` | `BoolProperty` | `0x559` |
| `DarkformMeshReference` | `ObjectProperty of CoreUObject.Object` | `0x560` |

`GA_ShellDownedBase_C`:

| Property | Type | Offset |
|---|---|---|
| `InteractionTriggerSphere` | `ObjectProperty of /Game/Sparta/Core/Common/Interactions/BP_GenericTrigger_Sphere.BP_GenericTrigger_Sphere_C` | `0x518` |
| `SeveredMontages` | `ArrayProperty` | `0x520` |
| `GrantedTags` | `GameplayTagContainer` | `0x530` |

`GA_ShellReviveBase_C`:

| Property | Type | Offset |
|---|---|---|
| `TargetRot` | `FRotator` | `0x518` |
| `TargetLoc` | `FVector` | `0x530` |
| `FallbackRevivePower` | `DoubleProperty` | `0x548` |
| `EventDataObject` | `SoftObjectProperty of CoreUObject.Object` | `0x550` |
| `IsCommittedFromEvent` | `BoolProperty` | `0x578` |
| `MyEventData` | `GameplayEventData` | `0x580` |

`GA_ShellReviveAction_C` (the interaction that actually performs the revive):

| Property | Type | Offset |
|---|---|---|
| `IndicatorActor` | `ObjectProperty of .../ShellRevive/BP_ShellReviveIndicator.BP_ShellReviveIndicator_C` | `0x518` |
| `IndicatorWidget` | `ObjectProperty of /Game/Sparta/UI/Player/WBP_Player_Bar_ShellRevive.WBP_Player_Bar_ShellRevive_C` | `0x520` |
| `InputTask` | `AbilityTask_WaitGameplayEvent` | `0x528` |
| `ForceReviveTask` | `AbilityTask_WaitGameplayEvent` | `0x530` |
| `ReviveMeterTask` | `AbilityTask_WaitAttributeChangeRatioThreshold` | `0x538` |
| `SessionSaveObject` | `ObjectProperty of /Game/Sparta/SaveObjects/BPSO_PlayerSession.BPSO_PlayerSession_C` | `0x540` |
| `CanRevive` | `BoolProperty` | `0x548` |
| `bListeningToDamage` | `BoolProperty` | `0x549` |
| `ShellReviveCap` | `IntProperty` | `0x54C` |
| `InitialMaxProgress` | `DoubleProperty` | `0x550` |
| `ShellReviveIndex` | `IntProperty` | `0x558` |

`GA_ShellTraversalBase_C` (the "player throws / carries the shell" traversal):

| Property | Type | Offset |
|---|---|---|
| `PlayerCharacter` | `ObjectProperty of Sparta.SpartaCharacter` | `0x518` |
| `ShellCharacter` | `ObjectProperty of Sparta.SpartaCharacter` | `0x520` |
| `RequirementTag` | `GameplayTag` | `0x528` |
| `OverrideShellClass` | `ClassProperty` | `0x530` |
| `DetachEventData` | `GameplayEventData` | `0x538` |
| `WeaponSlots` | `GameplayTagContainer` | `0x5E8` |
| `RiserAudioId` | `AudioEventId` | `0x608` |

**Sync reading of the revive system.** `GA_ShellReviveAction_C` drives the *shell revive meter* against
`UPlayerAttributeSet:ShellReviveCurrentProgress / ShellReviveMaxProgress / ShellReviveProgressGain /
ShellReviveMaxProgressMultiplier` (sec.2.3) with
`AbilityTask_WaitAttributeChangeRatioThreshold`, i.e. the revive is a local progress bar that fills by
dealing damage, not a "hold X to revive your friend" interaction. `ShellReviveCap` (`0x54C`) and
`ShellReviveIndex` (`0x558`) say how many revives are allowed/how many were used -- those two ints plus
`ShellReviveCurrentProgress` are the whole revive state a co-op layer needs. (No parameter of
`GA_ShellRevive*` takes another player: the dump shows no property pointing at a second
player controller -- GUESS: it is single-player revive only, so co-op revive has to be built on top.)

### 10.3 Shell swap

`UNKNOWN` -- there is **no** `GA_*` class named Swap/ShellChange/TransferShell. Tried:
`SELECT path FROM cls WHERE path LIKE '%/GA\_%' ESCAPE '\' AND path LIKE '/Game/Sparta/%' AND
(path LIKE '%Swap%' OR path LIKE '%ShellChange%' OR path LIKE '%TransferShell%')` -> 0 rows.
What does exist (from the native class list, section 1.1-1.2 of part 1):
`/Script/Sparta.ItemFragment_SetShellClass` and `/Script/Sparta.ItemFragment_SetStats` -- i.e. the shell
is changed through an **item fragment**, and the ability consumers are
`CharacterPersistantAbilitySetData.GiveAbilitySet/RemoveAbilitySet` (sec.1.2 of part 1). The exact
"swap" ability is therefore a Blueprint graph on the player character/controller, not a `GA_`.

Evidence - shell state abilities:
```
[000001F617BEAE10] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/ShellRevive/GA_ShellReviveAction.GA_ShellReviveAction_C [n: 247867] [c: 00007FF46EEC3D38] [or: 000001F4D1C1E070] [sps: 000001F4DC018E00]
[000001F61748CB80] ObjectProperty /Game/Sparta/Core/Player/Ability/ShellRevive/GA_ShellReviveAction.GA_ShellReviveAction_C:ReviveMeterTask [o: 538] [n: 672ECB] [c: 00007FF721DE9420] [owr: 000001F617BEAE10] [pc: 00007FF46EF95538]
[000001F4EB55FA50] IntProperty /Game/Sparta/Core/Player/Ability/ShellRevive/GA_ShellReviveAction.GA_ShellReviveAction_C:ShellReviveCap [o: 54C] [n: 641D1C] [c: 00007FF721DE91F0] [owr: 000001F617BEAE10]
[000001F4EB55F970] IntProperty /Game/Sparta/Core/Player/Ability/ShellRevive/GA_ShellReviveAction.GA_ShellReviveAction_C:ShellReviveIndex [o: 558] [n: 641D24] [c: 00007FF721DE91F0] [owr: 000001F617BEAE10]
[000001F61A5BCE20] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/ShellRevive/GA_ShellReviveBase.GA_ShellReviveBase_C [n: 9A8CC] [c: 00007FF46EEC3D38] [or: 000001F4D1C19AC0] [sps: 000001F4DC018E00]
[000001F61A5BBFE0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/ShellSevered/GA_ShellSeveredAnimationBase.GA_ShellSeveredAnimationBase_C [n: 9629A] [c: 00007FF46EEC3D38] [or: 000001F4D1C19020] [sps: 000001F615242DD0]
[000001F61A5BBC50] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/ShellSevered/GA_ShellSeveredAnimation_StrongOne.GA_ShellSeveredAnimation_StrongOne_C [n: 3FC887] [c: 00007FF46EEC3D38] [or: 000001F4D1C18F80] [sps: 000001F61A5BBFE0]
[000001F61A5BB1A0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/ShellTraversal/GA_ShellTraversalBase.GA_ShellTraversalBase_C [n: 34289E] [c: 00007FF46EEC3D38] [or: 000001F4D1C174F0] [sps: 000001F4DC018E00]
[000001F61C39E580] ObjectProperty /Game/Sparta/Core/Player/Ability/ShellTraversal/GA_ShellTraversalBase.GA_ShellTraversalBase_C:PlayerCharacter [o: 518] [n: 58A6E5] [c: 00007FF721DE9420] [owr: 000001F61A5BB1A0] [pc: 00007FF46F03C538]
[000001F61C39E500] ObjectProperty /Game/Sparta/Core/Player/Ability/ShellTraversal/GA_ShellTraversalBase.GA_ShellTraversalBase_C:ShellCharacter [o: 520] [n: 65206D] [c: 00007FF721DE9420] [owr: 000001F61A5BB1A0] [pc: 00007FF46F03C538]
[000001F61A5BAE10] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/ShellTraversal/GA_ShellTraversal_StrongOne.GA_ShellTraversal_StrongOne_C [n: 217C9D] [c: 00007FF46EEC3D38] [or: 000001F4D1C16AF0] [sps: 000001F61A5BB1A0]
[000001F61A5BEE30] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/ShellDowned/GA_ShellDownedBase.GA_ShellDownedBase_C [n: 269FDE] [c: 00007FF46EEC3D38] [or: 000001F4D1C169B0] [sps: 000001F4DC018E00]
[000001F76D899C40] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/ShellTraversal/GA_ShellTraversal_Default.GA_ShellTraversal_Default_C [n: DBDAC] [c: 00007FF46EEC3D38] [or: 000001F76D3D9B60] [sps: 000001F61A5BB1A0]
[000001F76D898E00] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/ShellSevered/GA_ShellSeveredBase.GA_ShellSeveredBase_C [n: 420884] [c: 00007FF46EEC3D38] [or: 000001F76D3DAF10] [sps: 000001F4DC018E00]
[000001F74E139300] ObjectProperty /Game/Sparta/Core/Player/Ability/ShellSevered/GA_ShellSeveredBase.GA_ShellSeveredBase_C:SpawnedShell [o: 518] [n: 5EDF73] [c: 00007FF721DE9420] [owr: 000001F76D898E00] [pc: 00007FF46EE52028]
[000001F74E139180] BoolProperty /Game/Sparta/Core/Player/Ability/ShellSevered/GA_ShellSeveredBase.GA_ShellSeveredBase_C:bInPairedAnim [o: 558] [n: 728DA5] [c: 00007FF721DE8D80] [owr: 000001F76D898E00]
[000001F4ADE21F90] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/ShellSevered/GA_ShellSeveredAnimation_TarGolemFight.GA_ShellSeveredAnimation_TarGolemFight_C [n: 412F2E] [c: 00007FF46EEC3D38] [or: 000001F8C9C94480] [sps: 000001F61A5BBFE0]
[000001F8CB63BFE0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/ShellSevered/GA_ShellSevered_Genessa.GA_ShellSevered_Genessa_C [n: 288DC2] [c: 00007FF46EEC3D38] [or: 000001F8CAEDF010] [sps: 000001F76D898E00]
[000001F8CB63B1A0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/ShellTraversal/GA_ShellTraversal_Genessa.GA_ShellTraversal_Genessa_C [n: 331FD7] [c: 00007FF46EEC3D38] [or: 000001F8CAEDFA10] [sps: 000001F61A5BB1A0]
[000001F7ED2C66D0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/ShellSevered/GA_ShellSevered_Thorn.GA_ShellSevered_Thorn_C [n: 41BED9] [c: 00007FF46EEC3D38] [or: 000001F7ED271C30] [sps: 000001F76D898E00]
[000001F8D751B8C0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/ShellRevive/GA_ShellRevive_Genessa.GA_ShellRevive_Genessa_C [n: 16A4B9] [c: 00007FF46EEC3D38] [or: 000001F8D7602860] [sps: 000001F61A5BCE20]
[000001F8D751B1A0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/ShellRevive/GA_ShellRevive_SavedByTheShell.GA_ShellRevive_SavedByTheShell_C [n: 159404] [c: 00007FF46EEC3D38] [or: 000001F8D7602810] [sps: 000001F61A5BCE20]
[000001F8D751F1C0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/ShellSevered/GA_ShellSeveredAnimation_Genessa.GA_ShellSeveredAnimation_Genessa_C [n: 65F7D] [c: 00007FF46EEC3D38] [or: 000001F8D76024F0] [sps: 000001F61A5BBFE0]
[000001F8D751A6F0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/ShellSevered/GA_ShellSeveredAnimation_TarredCorpse.GA_ShellSeveredAnimation_TarredCorpse_C [n: 365F0A] [c: 00007FF46EEC3D38] [or: 000001F8D7602450] [sps: 000001F61A5BBFE0]
[000001F8D7519FD0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/ShellTraversal/GA_ShellTraversal_CorruptedGenessa.GA_ShellTraversal_CorruptedGenessa_C [n: 43FA2E] [c: 00007FF46EEC3D38] [or: 000001F8D7602310] [sps: 000001F61A5BB1A0]
```

---

## 11. Tarstones, weapon abilities, artifacts and shell upgrades

### 11.1 `/Game/Sparta/Core/Tarstones/` - 49 ability classes

Three families, all keyed by the item that grants them:

* `GA_WeaponAbility_*` (parent `GA_WeaponAbilityBase_C`, itself a `GA_AttackBase_Melee_C`): the
  weapon-active ability (ClockworkChainsaw, ClockworkGrinder, DeadlyFlurry, HeavyStomps, HomingThrow,
  Katanas_A1/A2, PlummetStrike, SpiralSurge, StormStrike, WeaponThrow).
  `GA_WeaponAbilityBase_C` state: `ActiveWeaponAbilityEffect: ActiveGameplayEffectHandle` `0x8D8`,
  `bSkipCost: BoolProperty` `0x8E0`.
* `GA_Melee_*` / `GA_Sidearm_*` / `GA_Support_*` (parent `GA_UpgradeBase_C`): passive upgrade abilities.
* `GA_SidearmAttack_*` / `GA_SidearmActivation_*`: the tarstone-granted sidearm shots
  (AllySummon, Bombardment, ClusterBomb, Deadshot, LuteRiff, QuickShooting, Slingshot, StickyBomb,
  ThreadweaverShot) plus `GA_Sidearm_AllySummoner` / `GA_SummonedAlly_Handler` (a summon).

| Class | Parent | Own props |
|---|---|---|
| `/Game/Sparta/Core/Tarstones/Melee/Abilities/BP_EMOD_Blast.BP_EMOD_Blast_C` | `/Game/Sparta/Core/Tarstones/Melee/Abilities/BP_ElementalMechanicOverrideData.BP_ElementalMechanicOverrideData_C` | yes (count 13) |
| `/Game/Sparta/Core/Tarstones/Melee/Abilities/BP_EMOD_Cloud.BP_EMOD_Cloud_C` | `/Game/Sparta/Core/Tarstones/Melee/Abilities/BP_ElementalMechanicOverrideData.BP_ElementalMechanicOverrideData_C` | yes (count 7) |
| `/Game/Sparta/Core/Tarstones/Melee/Abilities/BP_EMOD_Projectile.BP_EMOD_Projectile_C` | `/Game/Sparta/Core/Tarstones/Melee/Abilities/BP_ElementalMechanicOverrideData.BP_ElementalMechanicOverrideData_C` | yes (count 11) |
| `/Game/Sparta/Core/Tarstones/Melee/Abilities/BP_EMOD_Shockwave.BP_EMOD_Shockwave_C` | `/Game/Sparta/Core/Tarstones/Melee/Abilities/BP_ElementalMechanicOverrideData.BP_ElementalMechanicOverrideData_C` | yes (count 10) |
| `/Game/Sparta/Core/Tarstones/Melee/Abilities/BP_ElementalMechanicData.BP_ElementalMechanicData_C` | `/Script/CoreUObject.Object` | yes (count 10) |
| `/Game/Sparta/Core/Tarstones/Melee/Abilities/BP_ElementalMechanicOverrideData.BP_ElementalMechanicOverrideData_C` | `/Script/Sparta.SpartaInstancedObject` | none |
| `/Game/Sparta/Core/Tarstones/Melee/Abilities/BP_HomingThrowHandler.BP_HomingThrowHandler_C` | `/Script/Engine.Actor` | yes (count 23) |
| `/Game/Sparta/Core/Tarstones/Melee/Abilities/GA_ElementalMechanicHandler.GA_ElementalMechanicHandler_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 11) |
| `/Game/Sparta/Core/Tarstones/Melee/Abilities/GA_Melee_HoldAttack_Heavy.GA_Melee_HoldAttack_Heavy_C` | `/Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C` | none |
| `/Game/Sparta/Core/Tarstones/Melee/Abilities/GA_WeaponAbility_ClockworkChainsaw.GA_WeaponAbility_ClockworkChainsaw_C` | `/Game/Sparta/Core/Player/Ability/GA_WeaponAbilityBase.GA_WeaponAbilityBase_C` | yes (count 9) |
| `/Game/Sparta/Core/Tarstones/Melee/Abilities/GA_WeaponAbility_ClockworkGrinder.GA_WeaponAbility_ClockworkGrinder_C` | `/Game/Sparta/Core/Tarstones/Melee/Abilities/GA_WeaponAbility_ClockworkChainsaw.GA_WeaponAbility_ClockworkChainsaw_C` | yes (count 9) |
| `/Game/Sparta/Core/Tarstones/Melee/Abilities/GA_WeaponAbility_DeadlyFlurry.GA_WeaponAbility_DeadlyFlurry_C` | `/Game/Sparta/Core/Player/Ability/GA_WeaponAbilityBase.GA_WeaponAbilityBase_C` | none |
| `/Game/Sparta/Core/Tarstones/Melee/Abilities/GA_WeaponAbility_HeavyStomps.GA_WeaponAbility_HeavyStomps_C` | `/Game/Sparta/Core/Player/Ability/GA_WeaponAbilityBase.GA_WeaponAbilityBase_C` | none |
| `/Game/Sparta/Core/Tarstones/Melee/Abilities/GA_WeaponAbility_HomingThrow.GA_WeaponAbility_HomingThrow_C` | `/Game/Sparta/Core/Player/Ability/GA_WeaponAbilityBase.GA_WeaponAbilityBase_C` | yes (count 10) |
| `/Game/Sparta/Core/Tarstones/Melee/Abilities/GA_WeaponAbility_Katanas_A1.GA_WeaponAbility_Katanas_A1_C` | `/Game/Sparta/Core/Player/Ability/GA_WeaponAbilityBase.GA_WeaponAbilityBase_C` | none |
| `/Game/Sparta/Core/Tarstones/Melee/Abilities/GA_WeaponAbility_Katanas_A2.GA_WeaponAbility_Katanas_A2_C` | `/Game/Sparta/Core/Player/Ability/GA_WeaponAbilityBase.GA_WeaponAbilityBase_C` | none |
| `/Game/Sparta/Core/Tarstones/Melee/Abilities/GA_WeaponAbility_PlummetStrike.GA_WeaponAbility_PlummetStrike_C` | `/Game/Sparta/Core/Player/Ability/GA_WeaponAbilityBase.GA_WeaponAbilityBase_C` | none |
| `/Game/Sparta/Core/Tarstones/Melee/Abilities/GA_WeaponAbility_SpiralSurge.GA_WeaponAbility_SpiralSurge_C` | `/Game/Sparta/Core/Player/Ability/GA_WeaponAbilityBase.GA_WeaponAbilityBase_C` | none |
| `/Game/Sparta/Core/Tarstones/Melee/Abilities/GA_WeaponAbility_StormStrike.GA_WeaponAbility_StormStrike_C` | `/Game/Sparta/Core/Player/Ability/GA_WeaponAbilityBase.GA_WeaponAbilityBase_C` | yes (count 3) |
| `/Game/Sparta/Core/Tarstones/Melee/Abilities/GA_WeaponAbility_WeaponThrow.GA_WeaponAbility_WeaponThrow_C` | `/Game/Sparta/Core/Player/Ability/GA_WeaponAbilityBase.GA_WeaponAbilityBase_C` | yes (count 6) |
| `/Game/Sparta/Core/Tarstones/Melee/Abilities/GA_WeaponBuffGranter.GA_WeaponBuffGranter_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 3) |
| `/Game/Sparta/Core/Tarstones/Melee/Effects/GE_Melee_HoldAttack_Light.GE_Melee_HoldAttack_Light_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Tarstones/Melee/Effects/GE_Melee_SuperMove_CritBonus.GE_Melee_SuperMove_CritBonus_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Tarstones/Melee/Effects/GE_Melee_SuperMove_CritChance.GE_Melee_SuperMove_CritChance_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Tarstones/Melee/Effects/GE_Unlock_Elemental_Weapon.GE_Unlock_Elemental_Weapon_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Tarstones/Melee/Effects/GE_Unlock_Elemental_Weapon_Bleed.GE_Unlock_Elemental_Weapon_Bleed_C` | `/Game/Sparta/Core/Tarstones/Melee/Effects/GE_Unlock_Elemental_Weapon.GE_Unlock_Elemental_Weapon_C` | none |
| `/Game/Sparta/Core/Tarstones/Melee/Effects/GE_Unlock_Elemental_Weapon_Burn.GE_Unlock_Elemental_Weapon_Burn_C` | `/Game/Sparta/Core/Tarstones/Melee/Effects/GE_Unlock_Elemental_Weapon.GE_Unlock_Elemental_Weapon_C` | none |
| `/Game/Sparta/Core/Tarstones/Melee/Effects/GE_Unlock_Elemental_Weapon_Curse.GE_Unlock_Elemental_Weapon_Curse_C` | `/Game/Sparta/Core/Tarstones/Melee/Effects/GE_Unlock_Elemental_Weapon.GE_Unlock_Elemental_Weapon_C` | none |
| `/Game/Sparta/Core/Tarstones/Melee/Effects/GE_Unlock_Elemental_Weapon_Frost.GE_Unlock_Elemental_Weapon_Frost_C` | `/Game/Sparta/Core/Tarstones/Melee/Effects/GE_Unlock_Elemental_Weapon.GE_Unlock_Elemental_Weapon_C` | none |
| `/Game/Sparta/Core/Tarstones/Melee/Effects/GE_Unlock_Elemental_Weapon_Lightning.GE_Unlock_Elemental_Weapon_Lightning_C` | `/Game/Sparta/Core/Tarstones/Melee/Effects/GE_Unlock_Elemental_Weapon.GE_Unlock_Elemental_Weapon_C` | none |
| `/Game/Sparta/Core/Tarstones/Melee/Effects/GE_Unlock_Elemental_Weapon_Phantom.GE_Unlock_Elemental_Weapon_Phantom_C` | `/Game/Sparta/Core/Tarstones/Melee/Effects/GE_Unlock_Elemental_Weapon.GE_Unlock_Elemental_Weapon_C` | none |
| `/Game/Sparta/Core/Tarstones/Melee/Effects/GE_Unlock_Elemental_Weapon_Poison.GE_Unlock_Elemental_Weapon_Poison_C` | `/Game/Sparta/Core/Tarstones/Melee/Effects/GE_Unlock_Elemental_Weapon.GE_Unlock_Elemental_Weapon_C` | none |
| `/Game/Sparta/Core/Tarstones/Melee/Effects/GE_Unlock_Elemental_Weapon_Stasis.GE_Unlock_Elemental_Weapon_Stasis_C` | `/Game/Sparta/Core/Tarstones/Melee/Effects/GE_Unlock_Elemental_Weapon.GE_Unlock_Elemental_Weapon_C` | none |
| `/Game/Sparta/Core/Tarstones/Melee/Effects/GE_Unlock_Elemental_Weapon_Trauma.GE_Unlock_Elemental_Weapon_Trauma_C` | `/Game/Sparta/Core/Tarstones/Melee/Effects/GE_Unlock_Elemental_Weapon.GE_Unlock_Elemental_Weapon_C` | none |
| `/Game/Sparta/Core/Tarstones/Melee/Effects/GE_Unlock_WeaponBuff.GE_Unlock_WeaponBuff_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Tarstones/Melee/GA_Melee_CriticalFlow.GA_Melee_CriticalFlow_C` | `/Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C` | yes (count 9) |
| `/Game/Sparta/Core/Tarstones/Melee/GA_Melee_GrantLeechOnKill.GA_Melee_GrantLeechOnKill_C` | `/Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C` | yes (count 3) |
| `/Game/Sparta/Core/Tarstones/Melee/GA_Melee_GrantWarpOnHit.GA_Melee_GrantWarpOnHit_C` | `/Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C` | yes (count 3) |
| `/Game/Sparta/Core/Tarstones/Melee/GA_Melee_InflictElemental.GA_Melee_InflictElemental_C` | `/Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C` | yes (count 14) |
| `/Game/Sparta/Core/Tarstones/Melee/GA_WeaponAbility_GrantWarpOnHit.GA_WeaponAbility_GrantWarpOnHit_C` | `/Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C` | yes (count 2) |
| `/Game/Sparta/Core/Tarstones/Melee/GE_Melee_CriticalFlow.GE_Melee_CriticalFlow_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Tarstones/Melee/ID_Melee_CriticalFlow.ID_Melee_CriticalFlow_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Melee/ID_Melee_DualWieldProficiency.ID_Melee_DualWieldProficiency_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Melee/ID_Melee_GrantLeechOnKill.ID_Melee_GrantLeechOnKill_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Melee/ID_Melee_GrantWarpOnHit.ID_Melee_GrantWarpOnHit_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Melee/ID_Melee_HeavyAttackFinisher_Critical.ID_Melee_HeavyAttackFinisher_Critical_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Melee/ID_Melee_HeavyAttackFinisher_Weak.ID_Melee_HeavyAttackFinisher_Weak_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Melee/ID_Melee_HeavyHoldAttack.ID_Melee_HeavyHoldAttack_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Melee/ID_Melee_InflictBleed.ID_Melee_InflictBleed_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Melee/ID_Melee_InflictBurn.ID_Melee_InflictBurn_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Melee/ID_Melee_InflictCurse.ID_Melee_InflictCurse_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Melee/ID_Melee_InflictFrost.ID_Melee_InflictFrost_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Melee/ID_Melee_InflictLightning.ID_Melee_InflictLightning_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Melee/ID_Melee_InflictPhantom.ID_Melee_InflictPhantom_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Melee/ID_Melee_InflictPoison.ID_Melee_InflictPoison_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Melee/ID_Melee_InflictStasis.ID_Melee_InflictStasis_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Melee/ID_Melee_InflictTrauma.ID_Melee_InflictTrauma_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Melee/ID_Melee_LightAttackFinisher_Break.ID_Melee_LightAttackFinisher_Break_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Melee/ID_Melee_LightAttackFinisher_Resolve.ID_Melee_LightAttackFinisher_Resolve_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Melee/ID_Melee_LightHoldAttack.ID_Melee_LightHoldAttack_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Melee/ID_Melee_Unyielding.ID_Melee_Unyielding_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Melee/ID_Melee_WeaponAbility_ClockworkChainsaw.ID_Melee_WeaponAbility_ClockworkChainsaw_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Melee/ID_Melee_WeaponAbility_ClockworkGrinder.ID_Melee_WeaponAbility_ClockworkGrinder_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Melee/ID_Melee_WeaponAbility_DeadlyFlurry.ID_Melee_WeaponAbility_DeadlyFlurry_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Melee/ID_Melee_WeaponAbility_HeavyStomps.ID_Melee_WeaponAbility_HeavyStomps_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Melee/ID_Melee_WeaponAbility_HomingThrow.ID_Melee_WeaponAbility_HomingThrow_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Melee/ID_Melee_WeaponAbility_Katanas.ID_Melee_WeaponAbility_Katanas_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Melee/ID_Melee_WeaponAbility_PlummetStrike.ID_Melee_WeaponAbility_PlummetStrike_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Melee/ID_Melee_WeaponAbility_SpiralSurge.ID_Melee_WeaponAbility_SpiralSurge_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Melee/ID_Melee_WeaponAbility_StormStrike.ID_Melee_WeaponAbility_StormStrike_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Melee/ID_Melee_WeaponAbility_WeaponThrow.ID_Melee_WeaponAbility_WeaponThrow_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmActivation_AllySummon.GA_SidearmActivation_AllySummon_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmSecondaryActivation.GA_SidearmSecondaryActivation_C` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmActivation_Bombardment.GA_SidearmActivation_Bombardment_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmSecondaryActivation.GA_SidearmSecondaryActivation_C` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmActivation_ClusterBomb.GA_SidearmActivation_ClusterBomb_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmSecondaryActivation.GA_SidearmSecondaryActivation_C` | yes (count 3) |
| `/Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmActivation_LuteRiff.GA_SidearmActivation_LuteRiff_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmSecondaryActivation.GA_SidearmSecondaryActivation_C` | yes (count 2) |
| `/Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmActivation_Slingshot.GA_SidearmActivation_Slingshot_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmSecondaryActivation.GA_SidearmSecondaryActivation_C` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmActivation_StickyBomb.GA_SidearmActivation_StickyBomb_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmSecondaryActivation.GA_SidearmSecondaryActivation_C` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmActivation_ThreadweaverShot.GA_SidearmActivation_ThreadweaverShot_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmSecondaryActivation.GA_SidearmSecondaryActivation_C` | yes (count 5) |
| `/Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmAttack_AllySummon.GA_SidearmAttack_AllySummon_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmRangedAttackBase.GA_SidearmRangedAttackBase_C` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmAttack_Bombardment.GA_SidearmAttack_Bombardment_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmRangedAttackBase.GA_SidearmRangedAttackBase_C` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmAttack_ClusterBomb.GA_SidearmAttack_ClusterBomb_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmRangedAttackBase.GA_SidearmRangedAttackBase_C` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmAttack_Deadshot_Charge.GA_SidearmAttack_Deadshot_Charge_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmRangedAttackBase.GA_SidearmRangedAttackBase_C` | yes (count 11) |
| `/Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmAttack_Deadshot_Shoot.GA_SidearmAttack_Deadshot_Shoot_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmRangedAttackBase.GA_SidearmRangedAttackBase_C` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmAttack_LuteRiff.GA_SidearmAttack_LuteRiff_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmRangedAttackBase.GA_SidearmRangedAttackBase_C` | yes (count 2) |
| `/Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmAttack_QuickShooting.GA_SidearmAttack_QuickShooting_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmRangedAttackBase.GA_SidearmRangedAttackBase_C` | yes (count 2) |
| `/Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmAttack_Slingshot.GA_SidearmAttack_Slingshot_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmRangedAttackBase.GA_SidearmRangedAttackBase_C` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmAttack_StickyBomb.GA_SidearmAttack_StickyBomb_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmRangedAttackBase.GA_SidearmRangedAttackBase_C` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmAttack_ThreadweaverShot_Burst.GA_SidearmAttack_ThreadweaverShot_Burst_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmRangedBurstAttackBase.GA_SidearmRangedBurstAttackBase_C` | yes (count 1) |
| `/Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_Sidearm_AllySummoner.GA_Sidearm_AllySummoner_C` | `/Game/Sparta/Core/Ability/GA_SpartaBase.GA_SpartaBase_C` | yes (count 6) |
| `/Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_Sidearm_QuickShooting_Handler.GA_Sidearm_QuickShooting_Handler_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 1) |
| `/Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SummonedAlly_Handler.GA_SummonedAlly_Handler_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 9) |
| `/Game/Sparta/Core/Tarstones/Sidearm/Abilities/GE_Sidearm_Bombardment.GE_Sidearm_Bombardment_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/Abilities/GE_Sidearm_ClusterBomb.GE_Sidearm_ClusterBomb_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/Abilities/GE_Sidearm_Deadshot.GE_Sidearm_Deadshot_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/Abilities/GE_Sidearm_QuickShooting.GE_Sidearm_QuickShooting_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/Abilities/GE_Sidearm_Slingshot.GE_Sidearm_Slingshot_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/Effects/GE_Unlock_Elemental_Sidearm.GE_Unlock_Elemental_Sidearm_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/Effects/GE_Unlock_Elemental_Sidearm_Bleed.GE_Unlock_Elemental_Sidearm_Bleed_C` | `/Game/Sparta/Core/Tarstones/Sidearm/Effects/GE_Unlock_Elemental_Sidearm.GE_Unlock_Elemental_Sidearm_C` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/Effects/GE_Unlock_Elemental_Sidearm_Burn.GE_Unlock_Elemental_Sidearm_Burn_C` | `/Game/Sparta/Core/Tarstones/Sidearm/Effects/GE_Unlock_Elemental_Sidearm.GE_Unlock_Elemental_Sidearm_C` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/Effects/GE_Unlock_Elemental_Sidearm_Curse.GE_Unlock_Elemental_Sidearm_Curse_C` | `/Game/Sparta/Core/Tarstones/Sidearm/Effects/GE_Unlock_Elemental_Sidearm.GE_Unlock_Elemental_Sidearm_C` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/Effects/GE_Unlock_Elemental_Sidearm_Frost.GE_Unlock_Elemental_Sidearm_Frost_C` | `/Game/Sparta/Core/Tarstones/Sidearm/Effects/GE_Unlock_Elemental_Sidearm.GE_Unlock_Elemental_Sidearm_C` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/Effects/GE_Unlock_Elemental_Sidearm_Lightning.GE_Unlock_Elemental_Sidearm_Lightning_C` | `/Game/Sparta/Core/Tarstones/Sidearm/Effects/GE_Unlock_Elemental_Sidearm.GE_Unlock_Elemental_Sidearm_C` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/Effects/GE_Unlock_Elemental_Sidearm_Phantom.GE_Unlock_Elemental_Sidearm_Phantom_C` | `/Game/Sparta/Core/Tarstones/Sidearm/Effects/GE_Unlock_Elemental_Sidearm.GE_Unlock_Elemental_Sidearm_C` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/Effects/GE_Unlock_Elemental_Sidearm_Poison.GE_Unlock_Elemental_Sidearm_Poison_C` | `/Game/Sparta/Core/Tarstones/Sidearm/Effects/GE_Unlock_Elemental_Sidearm.GE_Unlock_Elemental_Sidearm_C` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/Effects/GE_Unlock_Elemental_Sidearm_Trauma.GE_Unlock_Elemental_Sidearm_Trauma_C` | `/Game/Sparta/Core/Tarstones/Sidearm/Effects/GE_Unlock_Elemental_Sidearm.GE_Unlock_Elemental_Sidearm_C` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/Effects/GE_Unlock_SidearmBuff.GE_Unlock_SidearmBuff_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/GA_Sidearm_AttractionShot.GA_Sidearm_AttractionShot_C` | `/Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C` | yes (count 1) |
| `/Game/Sparta/Core/Tarstones/Sidearm/GA_Sidearm_Chainburst.GA_Sidearm_Chainburst_C` | `/Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C` | yes (count 6) |
| `/Game/Sparta/Core/Tarstones/Sidearm/GA_Sidearm_ExplosiveReaction.GA_Sidearm_ExplosiveReaction_C` | `/Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C` | yes (count 5) |
| `/Game/Sparta/Core/Tarstones/Sidearm/GA_Sidearm_InflictBreakOnCriticalHit.GA_Sidearm_InflictBreakOnCriticalHit_C` | `/Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C` | yes (count 2) |
| `/Game/Sparta/Core/Tarstones/Sidearm/GA_Sidearm_InflictElemental.GA_Sidearm_InflictElemental_C` | `/Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C` | yes (count 17) |
| `/Game/Sparta/Core/Tarstones/Sidearm/GA_Sidearm_PiercingShot.GA_Sidearm_PiercingShot_C` | `/Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/GA_Sidearm_RepulsiveShot.GA_Sidearm_RepulsiveShot_C` | `/Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/GE_Sidearm_Boombastic.GE_Sidearm_Boombastic_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/GE_Sidearm_Chainburst.GE_Sidearm_Chainburst_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/GE_Sidearm_ConcurrentCrossfire.GE_Sidearm_ConcurrentCrossfire_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/GE_Sidearm_Corrosion.GE_Sidearm_Corrosion_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/GE_Sidearm_Quickcharge.GE_Sidearm_Quickcharge_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/GE_Sidearm_Scattershot.GE_Sidearm_Scattershot_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/GE_Sidearm_Spiraling.GE_Sidearm_Spiraling_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_AllySummon.ID_Sidearm_AllySummon_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_AttractionShot.ID_Sidearm_AttractionShot_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_Bombardment.ID_Sidearm_Bombardment_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_Boombastic.ID_Sidearm_Boombastic_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_Chainburst.ID_Sidearm_Chainburst_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_ClusterBomb.ID_Sidearm_ClusterBomb_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_ConcurrentCrossfire.ID_Sidearm_ConcurrentCrossfire_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_Corrosion.ID_Sidearm_Corrosion_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_Deadshot.ID_Sidearm_Deadshot_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_ExplosiveReaction.ID_Sidearm_ExplosiveReaction_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_InflictBleed.ID_Sidearm_InflictBleed_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_InflictBreakOnCriticalHit.ID_Sidearm_InflictBreakOnCriticalHit_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_InflictBurn.ID_Sidearm_InflictBurn_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_InflictCurse.ID_Sidearm_InflictCurse_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_InflictFrost.ID_Sidearm_InflictFrost_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_InflictLightning.ID_Sidearm_InflictLightning_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_InflictPhantom.ID_Sidearm_InflictPhantom_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_InflictPoison.ID_Sidearm_InflictPoison_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_InflictTrauma.ID_Sidearm_InflictTrauma_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_LuteRiff.ID_Sidearm_LuteRiff_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_PiercingShot.ID_Sidearm_PiercingShot_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_QuickShooting.ID_Sidearm_QuickShooting_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_Quickcharge.ID_Sidearm_Quickcharge_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_RepulsiveShot.ID_Sidearm_RepulsiveShot_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_Scattershot.ID_Sidearm_Scattershot_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_Slingshot.ID_Sidearm_Slingshot_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_Spiraling.ID_Sidearm_Spiraling_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_StickyBomb.ID_Sidearm_StickyBomb_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_ThreadweaverShot.ID_Sidearm_ThreadweaverShot_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_Tiltshot.ID_Sidearm_Tiltshot_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Support/Abilities/GA_Support_OnKill_BonusStaggerDamage.GA_Support_OnKill_BonusStaggerDamage_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 1) |
| `/Game/Sparta/Core/Tarstones/Support/Abilities/GA_Support_OnKill_DamageReduction.GA_Support_OnKill_DamageReduction_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Tarstones/Support/Abilities/GA_Support_OnSidearmKill_GainResolve.GA_Support_OnSidearmKill_GainResolve_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 1) |
| `/Game/Sparta/Core/Tarstones/Support/Effects/GE_Support_DamageReduction.GE_Support_DamageReduction_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Tarstones/Support/Effects/GE_Support_GloomBoost.GE_Support_GloomBoost_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Tarstones/Support/Effects/GE_Support_GoldBoost.GE_Support_GoldBoost_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Tarstones/Support/Effects/GE_Support_OnKill_DamageReduction.GE_Support_OnKill_DamageReduction_C` | `/Game/Sparta/Core/Effects/StatusEffects/GE_SpartaStatusEffectBase.GE_SpartaStatusEffectBase_C` | none |
| `/Game/Sparta/Core/Tarstones/Support/Effects/GE_Support_OnKill_PoiseBonus.GE_Support_OnKill_PoiseBonus_C` | `/Game/Sparta/Core/Effects/StatusEffects/GE_SpartaStatusEffectBase.GE_SpartaStatusEffectBase_C` | none |
| `/Game/Sparta/Core/Tarstones/Support/Effects/GE_Support_RiposteDamage.GE_Support_RiposteDamage_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Tarstones/Support/ID_Support_DamageReduction.ID_Support_DamageReduction_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Support/ID_Support_GloomBoost.ID_Support_GloomBoost_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Support/ID_Support_GoldBoost.ID_Support_GoldBoost_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Support/ID_Support_OnKill_DamageReduction.ID_Support_OnKill_DamageReduction_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Support/ID_Support_OnKill_PoiseDamage.ID_Support_OnKill_PoiseDamage_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Support/ID_Support_OnSidearmKill_GainResolve.ID_Support_OnSidearmKill_GainResolve_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Tarstones/Support/ID_Support_RiposteDamage.ID_Support_RiposteDamage_C` | `/Script/Sparta.SpartaItemDefinition` | none |

Evidence - tarstone ability class rows:
```
[000001F61C270A30] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/Abilities/GA_ElementalMechanicHandler.GA_ElementalMechanicHandler_C [n: 6AB28] [c: 00007FF46EEC3D38] [or: 000001F757CCEFC0] [sps: 000001F4DC018E00]
[000001F7A75A3160] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/GA_Sidearm_InflictElemental.GA_Sidearm_InflictElemental_C [n: 17F744] [c: 00007FF46EEC3D38] [or: 000001F7A7124ED0] [sps: 000001F7A75A3FA0]
[000001F7E929AE10] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/GA_Melee_InflictElemental.GA_Melee_InflictElemental_C [n: 3259F5] [c: 00007FF46EEC3D38] [or: 000001F7F77B1640] [sps: 000001F7A75A3FA0]
[000001F76BD05500] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/Abilities/GA_WeaponBuffGranter.GA_WeaponBuffGranter_C [n: 444154] [c: 00007FF46EEC3D38] [or: 000001F7F77B2FE0] [sps: 000001F4DC018E00]
[000001F80CB7DC60] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmAttack_QuickShooting.GA_SidearmAttack_QuickShooting_C [n: 17B770] [c: 00007FF46EEC3D38] [or: 000001F80A6E9890] [sps: 000001F617F1D1B0]
[000001F80CB7FC70] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_Sidearm_QuickShooting_Handler.GA_Sidearm_QuickShooting_Handler_C [n: 231B4B] [c: 00007FF46EEC3D38] [or: 000001F80A6E9660] [sps: 000001F4DC018E00]
[000001F81A290310] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmActivation_Slingshot.GA_SidearmActivation_Slingshot_C [n: 3CBC72] [c: 00007FF46EEC3D38] [or: 000001F81A28AE70] [sps: 000001F81A290DC0]
[000001F81A29E710] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmAttack_Slingshot.GA_SidearmAttack_Slingshot_C [n: 18D095] [c: 00007FF46EEC3D38] [or: 000001F81A28B730] [sps: 000001F617F1D1B0]
[000001F8C0C32A40] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/Abilities/GA_Melee_HoldAttack_Heavy.GA_Melee_HoldAttack_Heavy_C [n: 2F0B59] [c: 00007FF46EEC3D38] [or: 000001F8D8E654C0] [sps: 000001F7A75A3FA0]
[000001F8C7B8B8C0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/Abilities/GA_WeaponAbility_ClockworkChainsaw.GA_WeaponAbility_ClockworkChainsaw_C [n: 154FC6] [c: 00007FF46EEC3D38] [or: 000001F8D8E653D0] [sps: 000001F8D73E7510]
[000001F8C1DCE380] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/Abilities/GA_WeaponAbility_ClockworkGrinder.GA_WeaponAbility_ClockworkGrinder_C [n: 2FE6A3] [c: 00007FF46EEC3D38] [or: 000001F8D8E66E10] [sps: 000001F8C7B8B8C0]
[000001F8AE842DD0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/Abilities/GA_WeaponAbility_DeadlyFlurry.GA_WeaponAbility_DeadlyFlurry_C [n: 2C5BC1] [c: 00007FF46EEC3D38] [or: 000001F8D8E67B80] [sps: 000001F8D73E7510]
[000001F8BD6A86E0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/Abilities/GA_WeaponAbility_HeavyStomps.GA_WeaponAbility_HeavyStomps_C [n: 1916A3] [c: 00007FF46EEC3D38] [or: 000001F8D8E67A40] [sps: 000001F8D73E7510]
[000001F8ACE86DF0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/Abilities/GA_WeaponAbility_HomingThrow.GA_WeaponAbility_HomingThrow_C [n: 202A1B] [c: 00007FF46EEC3D38] [or: 000001F8D8E678B0] [sps: 000001F8D73E7510]
[000001F7DC9D3880] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/Abilities/GA_WeaponAbility_PlummetStrike.GA_WeaponAbility_PlummetStrike_C [n: 342A93] [c: 00007FF46EEC3D38] [or: 000001F8D8E67630] [sps: 000001F8D73E7510]
[000001F7E2E34A50] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/Abilities/GA_WeaponAbility_SpiralSurge.GA_WeaponAbility_SpiralSurge_C [n: 49F647] [c: 00007FF46EEC3D38] [or: 000001F8D8E67540] [sps: 000001F8D73E7510]
[000001F7E2E34330] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/Abilities/GA_WeaponAbility_StormStrike.GA_WeaponAbility_StormStrike_C [n: A7B04] [c: 00007FF46EEC3D38] [or: 000001F8D8E674A0] [sps: 000001F8D73E7510]
[000001F7E30E3C10] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/Abilities/GA_WeaponAbility_WeaponThrow.GA_WeaponAbility_WeaponThrow_C [n: 1261B3] [c: 00007FF46EEC3D38] [or: 000001F8D8E67400] [sps: 000001F8D73E7510]
[000001F807E12A40] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/GA_Melee_CriticalFlow.GA_Melee_CriticalFlow_C [n: 38D0DE] [c: 00007FF46EEC3D38] [or: 000001F8D8E683A0] [sps: 000001F7A75A3FA0]
[000001F8C99AF8E0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/GA_Melee_GrantLeechOnKill.GA_Melee_GrantLeechOnKill_C [n: 1C576C] [c: 00007FF46EEC3D38] [or: 000001F8D8E681C0] [sps: 000001F7A75A3FA0]
[000001F8C99AF1C0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/GA_Melee_GrantWarpOnHit.GA_Melee_GrantWarpOnHit_C [n: 450B30] [c: 00007FF46EEC3D38] [or: 000001F8D8E680D0] [sps: 000001F7A75A3FA0]
[000001F8D90E7180] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/GA_WeaponAbility_GrantWarpOnHit.GA_WeaponAbility_GrantWarpOnHit_C [n: 342AA4] [c: 00007FF46EEC3D38] [or: 000001F8D8E67F90] [sps: 000001F7A75A3FA0]
[000001F8D90E0310] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SummonedAlly_Handler.GA_SummonedAlly_Handler_C [n: 12E710] [c: 00007FF46EEC3D38] [or: 000001F8D8E69CF0] [sps: 000001F4DC018E00]
[000001F8D90EE380] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_Sidearm_AllySummoner.GA_Sidearm_AllySummoner_C [n: 43B4F4] [c: 00007FF46EEC3D38] [or: 000001F8D8E69CA0] [sps: 000001F615243160]
[000001F8D90ED8D0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmActivation_AllySummon.GA_SidearmActivation_AllySummon_C [n: 4B50A6] [c: 00007FF46EEC3D38] [or: 000001F8D8E6AB00] [sps: 000001F81A290DC0]
[000001F8D90ED1B0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmActivation_Bombardment.GA_SidearmActivation_Bombardment_C [n: CEF56] [c: 00007FF46EEC3D38] [or: 000001F8D8E6AAB0] [sps: 000001F81A290DC0]
[000001F8D90ECA90] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmActivation_ClusterBomb.GA_SidearmActivation_ClusterBomb_C [n: 3CFFE4] [c: 00007FF46EEC3D38] [or: 000001F8D8E6A970] [sps: 000001F81A290DC0]
[000001F8D90EC370] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmActivation_LuteRiff.GA_SidearmActivation_LuteRiff_C [n: 214065] [c: 00007FF46EEC3D38] [or: 000001F8D8E6A830] [sps: 000001F81A290DC0]
[000001F8D90EBC50] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmActivation_StickyBomb.GA_SidearmActivation_StickyBomb_C [n: 325A04] [c: 00007FF46EEC3D38] [or: 000001F8D8E6A6F0] [sps: 000001F81A290DC0]
[000001F8D90EB530] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmActivation_ThreadweaverShot.GA_SidearmActivation_ThreadweaverShot_C [n: 26A44D] [c: 00007FF46EEC3D38] [or: 000001F8D8E6B690] [sps: 000001F81A290DC0]
[000001F8D90EAA80] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmAttack_AllySummon.GA_SidearmAttack_AllySummon_C [n: 23A313] [c: 00007FF46EEC3D38] [or: 000001F8D8E6B410] [sps: 000001F617F1D1B0]
[000001F8D90EA360] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmAttack_Bombardment.GA_SidearmAttack_Bombardment_C [n: CEF68] [c: 00007FF46EEC3D38] [or: 000001F8D8E6B2D0] [sps: 000001F617F1D1B0]
[000001F8D90E9C40] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmAttack_ClusterBomb.GA_SidearmAttack_ClusterBomb_C [n: 29A249] [c: 00007FF46EEC3D38] [or: 000001F8D8E6B230] [sps: 000001F617F1D1B0]
[000001F8D90E9520] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmAttack_Deadshot_Shoot.GA_SidearmAttack_Deadshot_Shoot_C [n: 2BD2DE] [c: 00007FF46EEC3D38] [or: 000001F8D8E6C310] [sps: 000001F617F1D1B0]
[000001F8D90E8E00] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmAttack_Deadshot_Charge.GA_SidearmAttack_Deadshot_Charge_C [n: 1C577B] [c: 00007FF46EEC3D38] [or: 000001F8D8E6C1D0] [sps: 000001F617F1D1B0]
[000001F8D90E8350] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmAttack_LuteRiff.GA_SidearmAttack_LuteRiff_C [n: 17F71C] [c: 00007FF46EEC3D38] [or: 000001F8D8E6F3D0] [sps: 000001F617F1D1B0]
[000001F8D90E78A0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmAttack_StickyBomb.GA_SidearmAttack_StickyBomb_C [n: FE6D5] [c: 00007FF46EEC3D38] [or: 000001F8CF3510A0] [sps: 000001F617F1D1B0]
[000001F8D90EF8E0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/Abilities/GA_SidearmAttack_ThreadweaverShot_Burst.GA_SidearmAttack_ThreadweaverShot_Burst_C [n: 23EF1C] [c: 00007FF46EEC3D38] [or: 000001F8CF351000] [sps: 000001F8D7694A50]
[000001F8DB300DC0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/GA_Sidearm_AttractionShot.GA_Sidearm_AttractionShot_C [n: 1FE850] [c: 00007FF46EEC3D38] [or: 000001F8CF3527C0] [sps: 000001F7A75A3FA0]
[000001F8DB3006A0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/GA_Sidearm_Chainburst.GA_Sidearm_Chainburst_C [n: 188951] [c: 00007FF46EEC3D38] [or: 000001F8CF352270] [sps: 000001F7A75A3FA0]
[000001F8DB300310] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/GA_Sidearm_ExplosiveReaction.GA_Sidearm_ExplosiveReaction_C [n: A3748] [c: 00007FF46EEC3D38] [or: 000001F8CF351FF0] [sps: 000001F7A75A3FA0]
[000001F8DB30E380] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/GA_Sidearm_InflictBreakOnCriticalHit.GA_Sidearm_InflictBreakOnCriticalHit_C [n: 3F3B73] [c: 00007FF46EEC3D38] [or: 000001F8CF353350] [sps: 000001F7A75A3FA0]
[000001F8DB30DC60] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/GA_Sidearm_PiercingShot.GA_Sidearm_PiercingShot_C [n: 4132FD] [c: 00007FF46EEC3D38] [or: 000001F8CF353260] [sps: 000001F7A75A3FA0]
[000001F8DB308350] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/GA_Sidearm_RepulsiveShot.GA_Sidearm_RepulsiveShot_C [n: 49B1DA] [c: 00007FF46EEC3D38] [or: 000001F8CF353170] [sps: 000001F7A75A3FA0]
[000001F8DB53EAA0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Support/Abilities/GA_Support_OnKill_BonusStaggerDamage.GA_Support_OnKill_BonusStaggerDamage_C [n: 77B3D] [c: 00007FF46EEC3D38] [or: 000001F8CF355880] [sps: 000001F4DC018E00]
[000001F8DB53D8D0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Support/Abilities/GA_Support_OnKill_DamageReduction.GA_Support_OnKill_DamageReduction_C [n: 3E17A8] [c: 00007FF46EEC3D38] [or: 000001F8CF355380] [sps: 000001F4DC018E00]
[000001F8DB53D1B0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Support/Abilities/GA_Support_OnSidearmKill_GainResolve.GA_Support_OnSidearmKill_GainResolve_C [n: BE4A1] [c: 00007FF46EEC3D38] [or: 000001F8CF3562D0] [sps: 000001F4DC018E00]
```

### 11.2 `/Game/Sparta/Core/Player/Artifacts/` - 25 ability classes

One `GA_Artifact_<Name>` per artifact. Parents are either `GA_SpartaBase_C` (the ones that need an
attack/montage) or `GA_SpartaAbility_C` (pure passive). None carries a cost field of its own; the
artifact behaviour lives in the Blueprint graph + the item definition.

| Class | Parent | Own props |
|---|---|---|
| `/Game/Sparta/Core/Player/Artifacts/BPC_ArtifactGiver.BPC_ArtifactGiver_C` | `/Script/Engine.ActorComponent` | yes (count 13) |
| `/Game/Sparta/Core/Player/Artifacts/BP_ArtifactsComponent.BP_ArtifactsComponent_C` | `/Script/Sparta.ArtifactComponent` | yes (count 11) |
| `/Game/Sparta/Core/Player/Artifacts/GA_Artifact_AcolytesThorns.GA_Artifact_AcolytesThorns_C` | `/Game/Sparta/Core/Ability/GA_SpartaBase.GA_SpartaBase_C` | none |
| `/Game/Sparta/Core/Player/Artifacts/GA_Artifact_AsceticsAlchemyStone.GA_Artifact_AsceticsAlchemyStone_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Artifacts/GA_Artifact_ClockworkSight.GA_Artifact_ClockworkSight_C` | `/Game/Sparta/Core/Ability/GA_SpartaBase.GA_SpartaBase_C` | none |
| `/Game/Sparta/Core/Player/Artifacts/GA_Artifact_CrestOfBrotherhood.GA_Artifact_CrestOfBrotherhood_C` | `/Game/Sparta/Core/Ability/GA_SpartaBase.GA_SpartaBase_C` | yes (count 2) |
| `/Game/Sparta/Core/Player/Artifacts/GA_Artifact_FoulCenser.GA_Artifact_FoulCenser_C` | `/Game/Sparta/Core/Ability/GA_SpartaBase.GA_SpartaBase_C` | yes (count 1) |
| `/Game/Sparta/Core/Player/Artifacts/GA_Artifact_GenessasRosary.GA_Artifact_GenessasRosary_C` | `/Game/Sparta/Core/Ability/GA_SpartaBase.GA_SpartaBase_C` | yes (count 2) |
| `/Game/Sparta/Core/Player/Artifacts/GA_Artifact_HungryTarstone.GA_Artifact_HungryTarstone_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 2) |
| `/Game/Sparta/Core/Player/Artifacts/GA_Artifact_ImperialWrit.GA_Artifact_ImperialWrit_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Artifacts/GA_Artifact_MartyrsCursingCharm.GA_Artifact_MartyrsCursingCharm_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Artifacts/GA_Artifact_MoltenShaft.GA_Artifact_MoltenShaft_C` | `/Game/Sparta/Core/Ability/GA_SpartaBase.GA_SpartaBase_C` | yes (count 2) |
| `/Game/Sparta/Core/Player/Artifacts/GA_Artifact_PerfumedCenser.GA_Artifact_PerfumedCenser_C` | `/Game/Sparta/Core/Ability/GA_SpartaBase.GA_SpartaBase_C` | yes (count 2) |
| `/Game/Sparta/Core/Player/Artifacts/GA_Artifact_SavedByTheShell.GA_Artifact_SavedByTheShell_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 3) |
| `/Game/Sparta/Core/Player/Artifacts/GA_Artifact_ScholarsAmulet.GA_Artifact_ScholarsAmulet_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 2) |
| `/Game/Sparta/Core/Player/Artifacts/GA_Artifact_ScriptureOfPenance.GA_Artifact_ScriptureOfPenance_C` | `/Game/Sparta/Core/Ability/GA_SpartaBase.GA_SpartaBase_C` | none |
| `/Game/Sparta/Core/Player/Artifacts/GA_Artifact_SealOfTheStonebound.GA_Artifact_SealOfTheStonebound_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Artifacts/GA_Artifact_SolemnOffering.GA_Artifact_SolemnOffering_C` | `/Game/Sparta/Core/Ability/GA_SpartaBase.GA_SpartaBase_C` | none |
| `/Game/Sparta/Core/Player/Artifacts/GA_Artifact_TarForgedIdol.GA_Artifact_TarForgedIdol_C` | `/Game/Sparta/Core/Ability/GA_SpartaBase.GA_SpartaBase_C` | none |
| `/Game/Sparta/Core/Player/Artifacts/GA_Artifact_TarsaltCoating.GA_Artifact_TarsaltCoating_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Artifacts/GA_Artifact_TarsusGauntlet.GA_Artifact_TarsusGauntlet_C` | `/Game/Sparta/Core/Ability/GA_SpartaBase.GA_SpartaBase_C` | none |
| `/Game/Sparta/Core/Player/Artifacts/GA_Artifact_TatteredEffigyOfTheScholar.GA_Artifact_TatteredEffigyOfTheScholar_C` | `/Game/Sparta/Core/Ability/GA_SpartaBase.GA_SpartaBase_C` | none |
| `/Game/Sparta/Core/Player/Artifacts/GA_Artifact_Twicebornsrequital.GA_Artifact_Twicebornsrequital_C` | `/Game/Sparta/Core/Ability/GA_SpartaBase.GA_SpartaBase_C` | yes (count 2) |
| `/Game/Sparta/Core/Player/Artifacts/GA_Artifact_TwinSestersStone.GA_Artifact_TwinSestersStone_C` | `/Game/Sparta/Core/Ability/GA_SpartaBase.GA_SpartaBase_C` | yes (count 2) |
| `/Game/Sparta/Core/Player/Artifacts/GE_Artifact_BlackBlood.GE_Artifact_BlackBlood_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Player/Artifacts/GE_Artifact_ClockworkSight.GE_Artifact_ClockworkSight_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Player/Artifacts/GE_Artifact_ClockworkSightCooldown.GE_Artifact_ClockworkSightCooldown_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Player/Artifacts/GE_Artifact_EredrimsInitiateTalisman.GE_Artifact_EredrimsInitiateTalisman_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Player/Artifacts/GE_Artifact_Mango.GE_Artifact_Mango_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Player/Artifacts/GE_Artifact_SealOfTheStonebound.GE_Artifact_SealOfTheStonebound_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Player/Artifacts/GE_Artifact_SealOfTheStonebound_Cooldown.GE_Artifact_SealOfTheStonebound_Cooldown_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Player/Artifacts/GE_Artifact_SolomonsEffigy.GE_Artifact_SolomonsEffigy_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Player/Artifacts/GE_Artifact_TwicebornsRequital.GE_Artifact_TwicebornsRequital_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Player/Artifacts/ID_Artifact_AcolytesThorns.ID_Artifact_AcolytesThorns_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/ID_Artifact_AsceticsAlchemyStone.ID_Artifact_AsceticsAlchemyStone_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/ID_Artifact_BlackBlood.ID_Artifact_BlackBlood_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/ID_Artifact_ClockworkSight.ID_Artifact_ClockworkSight_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/ID_Artifact_CrestOfBrotherhood.ID_Artifact_CrestOfBrotherhood_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/ID_Artifact_EredrimsInitiateTalisman.ID_Artifact_EredrimsInitiateTalisman_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/ID_Artifact_FoulCenser.ID_Artifact_FoulCenser_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/ID_Artifact_GenessasRosary.ID_Artifact_GenessasRosary_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/ID_Artifact_GentlemansTeaCup.ID_Artifact_GentlemansTeaCup_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/ID_Artifact_GrishaGift.ID_Artifact_GrishaGift_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/ID_Artifact_HungryTarstone.ID_Artifact_HungryTarstone_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/ID_Artifact_ImperialWrit.ID_Artifact_ImperialWrit_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/ID_Artifact_Mango.ID_Artifact_Mango_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/ID_Artifact_MartyrsCursingCharm.ID_Artifact_MartyrsCursingCharm_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/ID_Artifact_MoltenShaft.ID_Artifact_MoltenShaft_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/ID_Artifact_PerfumedCenser.ID_Artifact_PerfumedCenser_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/ID_Artifact_SavedByTheShell.ID_Artifact_SavedByTheShell_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/ID_Artifact_ScholarsAmulet.ID_Artifact_ScholarsAmulet_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/ID_Artifact_ScriptureOfPenance.ID_Artifact_ScriptureOfPenance_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/ID_Artifact_SealOfTheStonebound.ID_Artifact_SealOfTheStonebound_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/ID_Artifact_SolemnOffering.ID_Artifact_SolemnOffering_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/ID_Artifact_SolomonsEffigy.ID_Artifact_SolomonsEffigy_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/ID_Artifact_TarForgedIdol.ID_Artifact_TarForgedIdol_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/ID_Artifact_TarsaltCoating.ID_Artifact_TarsaltCoating_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/ID_Artifact_TarsusGauntlet.ID_Artifact_TarsusGauntlet_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/ID_Artifact_TatteredEffigyOfTheScholar.ID_Artifact_TatteredEffigyOfTheScholar_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/ID_Artifact_TwicebornsRequital.ID_Artifact_TwicebornsRequital_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/ID_Artifact_TwinSestersStone.ID_Artifact_TwinSestersStone_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/NarrativeArtifacts/GA_Artifact_GoldBoost.GA_Artifact_GoldBoost_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Artifacts/NarrativeArtifacts/GA_Artifact_HealOnMelee.GA_Artifact_HealOnMelee_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Artifacts/NarrativeArtifacts/GA_Artifact_XpBoost.GA_Artifact_XpBoost_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Artifacts/NarrativeArtifacts/GE_Artifact_IncreaseCritChanceIOnSprintAttack.GE_Artifact_IncreaseCritChanceIOnSprintAttack_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Player/Artifacts/NarrativeArtifacts/GE_Artifact_LightningImmunity.GE_Artifact_LightningImmunity_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Player/Artifacts/NarrativeArtifacts/GE_Artifact_PoisonBuff.GE_Artifact_PoisonBuff_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Player/Artifacts/NarrativeArtifacts/GE_Artifact_PoisonImmunity.GE_Artifact_PoisonImmunity_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Core/Player/Artifacts/NarrativeArtifacts/ID_Artifact_GoldBoost.ID_Artifact_GoldBoost_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/NarrativeArtifacts/ID_Artifact_GorfsBlessing.ID_Artifact_GorfsBlessing_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/NarrativeArtifacts/ID_Artifact_HealOnMelee.ID_Artifact_HealOnMelee_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/NarrativeArtifacts/ID_Artifact_IncreaseCritChanceOnSprintAttack.ID_Artifact_IncreaseCritChanceOnSprintAttack_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/NarrativeArtifacts/ID_Artifact_LightningImmunity.ID_Artifact_LightningImmunity_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/NarrativeArtifacts/ID_Artifact_PoisonBuff.ID_Artifact_PoisonBuff_C` | `/Script/Sparta.SpartaItemDefinition` | none |
| `/Game/Sparta/Core/Player/Artifacts/NarrativeArtifacts/ID_Artifact_XpBoost.ID_Artifact_XpBoost_C` | `/Script/Sparta.SpartaItemDefinition` | none |

Evidence - artifact ability class rows:
```
[000001F7ED4F34F0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/GA_Artifact_AcolytesThorns.GA_Artifact_AcolytesThorns_C [n: 1B89F9] [c: 00007FF46EEC3D38] [or: 000001F7ED344390] [sps: 000001F615243160]
[000001F7ED4F2DD0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/GA_Artifact_AsceticsAlchemyStone.GA_Artifact_AsceticsAlchemyStone_C [n: 2C10F0] [c: 00007FF46EEC3D38] [or: 000001F7ED3442A0] [sps: 000001F4DC018E00]
[000001F7ED4F1F90] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/GA_Artifact_ClockworkSight.GA_Artifact_ClockworkSight_C [n: 2F951E] [c: 00007FF46EEC3D38] [or: 000001F7ED344160] [sps: 000001F615243160]
[000001F7ED4F1870] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/GA_Artifact_CrestOfBrotherhood.GA_Artifact_CrestOfBrotherhood_C [n: 3F3718] [c: 00007FF46EEC3D38] [or: 000001F7ED343FD0] [sps: 000001F615243160]
[000001F7ED4F1150] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/GA_Artifact_FoulCenser.GA_Artifact_FoulCenser_C [n: 13FB4C] [c: 00007FF46EEC3D38] [or: 000001F7ED343E90] [sps: 000001F615243160]
[000001F7ED4F0A30] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/GA_Artifact_GenessasRosary.GA_Artifact_GenessasRosary_C [n: 31CA01] [c: 00007FF46EEC3D38] [or: 000001F7ED3451A0] [sps: 000001F615243160]
[000001F7ED4FDFF0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/GA_Artifact_HungryTarstone.GA_Artifact_HungryTarstone_C [n: 20F4E5] [c: 00007FF46EEC3D38] [or: 000001F7ED345010] [sps: 000001F4DC018E00]
[000001F7ED4FD1B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/GA_Artifact_ImperialWrit.GA_Artifact_ImperialWrit_C [n: 20706D] [c: 00007FF46EEC3D38] [or: 000001F7ED344ED0] [sps: 000001F4DC018E00]
[000001F7ED4FCA90] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/GA_Artifact_MartyrsCursingCharm.GA_Artifact_MartyrsCursingCharm_C [n: 177032] [c: 00007FF46EEC3D38] [or: 000001F7ED344DE0] [sps: 000001F4DC018E00]
[000001F7ED4FC370] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/GA_Artifact_MoltenShaft.GA_Artifact_MoltenShaft_C [n: 18CE3A] [c: 00007FF46EEC3D38] [or: 000001F7ED344CA0] [sps: 000001F615243160]
[000001F7ED4FBC50] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/GA_Artifact_PerfumedCenser.GA_Artifact_PerfumedCenser_C [n: 10B5DB] [c: 00007FF46EEC3D38] [or: 000001F7ED344B60] [sps: 000001F615243160]
[000001F7ED4FB530] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/GA_Artifact_SavedByTheShell.GA_Artifact_SavedByTheShell_C [n: D30E1] [c: 00007FF46EEC3D38] [or: 000001F7ED344A20] [sps: 000001F4DC018E00]
[000001F7ED4FA6F0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/GA_Artifact_ScholarsAmulet.GA_Artifact_ScholarsAmulet_C [n: 44C7F9] [c: 00007FF46EEC3D38] [or: 000001F7ED344930] [sps: 000001F4DC018E00]
[000001F7ED4F9C40] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/GA_Artifact_ScriptureOfPenance.GA_Artifact_ScriptureOfPenance_C [n: 1AB654] [c: 00007FF46EEC3D38] [or: 000001F7ED345C40] [sps: 000001F615243160]
[000001F7ED4F8E00] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/GA_Artifact_SealOfTheStonebound.GA_Artifact_SealOfTheStonebound_C [n: 400F89] [c: 00007FF46EEC3D38] [or: 000001F7ED345B50] [sps: 000001F4DC018E00]
[000001F7ED4F86E0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/GA_Artifact_SolemnOffering.GA_Artifact_SolemnOffering_C [n: 48D993] [c: 00007FF46EEC3D38] [or: 000001F7ED345A10] [sps: 000001F615243160]
[000001F7ED4F7FC0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/GA_Artifact_TarForgedIdol.GA_Artifact_TarForgedIdol_C [n: 284928] [c: 00007FF46EEC3D38] [or: 000001F7ED345970] [sps: 000001F615243160]
[000001F7ED4F78A0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/GA_Artifact_TarsaltCoating.GA_Artifact_TarsaltCoating_C [n: 22D135] [c: 00007FF46EEC3D38] [or: 000001F7ED345920] [sps: 000001F4DC018E00]
[000001F7ED4FF8E0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/GA_Artifact_TarsusGauntlet.GA_Artifact_TarsusGauntlet_C [n: 320D2C] [c: 00007FF46EEC3D38] [or: 000001F7ED3457E0] [sps: 000001F615243160]
[000001F7ED4FF1C0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/GA_Artifact_TatteredEffigyOfTheScholar.GA_Artifact_TatteredEffigyOfTheScholar_C [n: 1D65F6] [c: 00007FF46EEC3D38] [or: 000001F7ED345650] [sps: 000001F615243160]
[000001F7ED5A6A60] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/GA_Artifact_Twicebornsrequital.GA_Artifact_Twicebornsrequital_C [n: 2026AE] [c: 00007FF46EEC3D38] [or: 000001F7ED3454C0] [sps: 000001F615243160]
[000001F7ED5A5FB0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/GA_Artifact_TwinSestersStone.GA_Artifact_TwinSestersStone_C [n: 33202C] [c: 00007FF46EEC3D38] [or: 000001F7ED345380] [sps: 000001F615243160]
[000001F7ED5A8350] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/NarrativeArtifacts/GA_Artifact_GoldBoost.GA_Artifact_GoldBoost_C [n: 21BB7F] [c: 00007FF46EEC3D38] [or: 000001F7ED348EE0] [sps: 000001F4DC018E00]
[000001F7ED5A7C30] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/NarrativeArtifacts/GA_Artifact_HealOnMelee.GA_Artifact_HealOnMelee_C [n: 11CF61] [c: 00007FF46EEC3D38] [or: 000001F7ED348E40] [sps: 000001F4DC018E00]
[000001F7ED5AFC70] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/NarrativeArtifacts/GA_Artifact_XpBoost.GA_Artifact_XpBoost_C [n: 31833B] [c: 00007FF46EEC3D38] [or: 000001F7ED348D00] [sps: 000001F4DC018E00]
```

### 11.3 `/Game/Sparta/Core/Player/Upgrades/` - 70 ability classes (shell skills / seals / weapon upgrades)

`GA_UpgradeBase_C` (parent `/Script/Sparta.SpartaGameplayAbility`) is the base of every upgrade ability:

| Property | Type | Offset |
|---|---|---|
| `UpgradeID` | `NameProperty` | `0x510` |
| `PlayerCharacter` | `ObjectProperty of Engine.Actor` | `0x518` |
| `ItemDef` | `SoftClassProperty` | `0x520` |

`GA_ShellUpgrade_Base_C` (parent `GA_SpartaAbility_C`) and `GA_ShellAbility_InflictEffect_C` are the
two intermediate bases; `GA_SlayerSeal_*` (Handler/Charge/Attack) is the seal ability, `GA_Eredrim_*`,
`GA_Genessa_*`, `GA_Gragu_*`, `GA_Lazlo_*`, `GA_Proxima_*`, `GA_Sariel_*`, `GA_Smert_*`, `GA_Tiel_*` are
the per-shell skill trees, and `GA_Sidearm_*` / `GA_ParasiteGun_*` / `GA_TrebuchaxeActivation_Tiltshot`
are weapon upgrades. `GA_UpgradeBase.ItemDef` (`SoftClassProperty`, `0x520`) is the link back to the
item definition -- that id is what a co-op layer should replicate (the item), not the ability.

Note there is **no** `Cooldown`/`Cost` override on these classes: they inherit
`USpartaGameplayAbility.Cost/Cooldown/CooldownDuration` (sec.1.1 of part 1), so the numbers are Blueprint
default values, which the object dump does not print.

The table below lists the 70 GA_* classes only; the folder also contains 344 non-ability classes
(ID_* item definitions, IF_* fragments, GE_* effects), which are the items report area.

| Class | Parent | Own props |
|---|---|---|
| `/Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C` | `/Script/Sparta.SpartaGameplayAbility` | yes (count 3) |
| `/Game/Sparta/Core/Player/Upgrades/Melee/Passives/Perforation/GA_Melee_InflictPerforation.GA_Melee_InflictPerforation_C` | `/Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C` | yes (count 2) |
| `/Game/Sparta/Core/Player/Upgrades/Seals/GA_SlayerSeal_Attack.GA_SlayerSeal_Attack_C` | `/Game/Sparta/Core/Ability/GA_AttackBase_Melee.GA_AttackBase_Melee_C` | yes (count 3) |
| `/Game/Sparta/Core/Player/Upgrades/Seals/GA_SlayerSeal_Charge.GA_SlayerSeal_Charge_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 7) |
| `/Game/Sparta/Core/Player/Upgrades/Seals/GA_SlayerSeal_Handler.GA_SlayerSeal_Handler_C` | `/Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C` | yes (count 7) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/Abilities/GA_Eredrim_Alacrity.GA_Eredrim_Alacrity_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | none |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/Abilities/GA_Eredrim_Anguish.GA_Eredrim_Anguish_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | none |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/Abilities/GA_Eredrim_Carnage.GA_Eredrim_Carnage_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | none |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/Abilities/GA_Eredrim_Consecration.GA_Eredrim_Consecration_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | none |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/Abilities/GA_Eredrim_Massacre.GA_Eredrim_Massacre_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | yes (count 1) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/Abilities/GA_Eredrim_Oppression.GA_Eredrim_Oppression_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | none |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/Abilities/GA_Eredrim_Resonance.GA_Eredrim_Resonance_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | none |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/Abilities/GA_Eredrim_SealAffinity.GA_Eredrim_SealAffinity_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | yes (count 1) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellAbility_InflictEffect.GA_ShellAbility_InflictEffect_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 4) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 2) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Genessa/Abilities/GA_Genessa_Affinity.GA_Genessa_Affinity_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | yes (count 1) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Genessa/Abilities/GA_Genessa_Alteration.GA_Genessa_Alteration_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | yes (count 2) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Genessa/Abilities/GA_Genessa_Augmentation.GA_Genessa_Augmentation_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | none |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Genessa/Abilities/GA_Genessa_Causality.GA_Genessa_Causality_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | none |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Genessa/Abilities/GA_Genessa_Communion.GA_Genessa_Communion_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | yes (count 1) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Genessa/Abilities/GA_Genessa_Distraction.GA_Genessa_Distraction_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | yes (count 1) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Genessa/Abilities/GA_Genessa_Distraction_AI.GA_Genessa_Distraction_AI_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | yes (count 6) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Genessa/Abilities/GA_Genessa_Instigation.GA_Genessa_Instigation_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | yes (count 1) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Genessa/Abilities/GA_Genessa_Intangible.GA_Genessa_Intangible_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | yes (count 3) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Genessa/Abilities/GA_Genessa_Mirage.GA_Genessa_Mirage_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | none |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Genessa/Abilities/GA_Genessa_Timeless.GA_Genessa_Timeless_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | none |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Gragu/Abilities/GA_Gragu_Anger.GA_Gragu_Anger_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellAbility_InflictEffect.GA_ShellAbility_InflictEffect_C` | none |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Gragu/Abilities/GA_Gragu_Bloodlust.GA_Gragu_Bloodlust_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 1) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Gragu/Abilities/GA_Gragu_Devourer.GA_Gragu_Devourer_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 3) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Gragu/Abilities/GA_Gragu_Fortify.GA_Gragu_Fortify_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | none |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Gragu/Abilities/GA_Gragu_Heartless_Handler.GA_Gragu_Heartless_Handler_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | yes (count 7) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Gragu/Abilities/GA_Gragu_Heartwork.GA_Gragu_Heartwork_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | none |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Gragu/Abilities/GA_Gragu_Hunger.GA_Gragu_Hunger_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | none |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Lazlo/Ability/GA_Lazlo_Detonation.GA_Lazlo_Detonation_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | yes (count 1) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Lazlo/Ability/GA_Lazlo_Furnace.GA_Lazlo_Furnace_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | yes (count 3) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Lazlo/Ability/GA_Lazlo_InnerFire.GA_Lazlo_InnerFire_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | none |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Lazlo/Ability/GA_Lazlo_Rekindle.GA_Lazlo_Rekindle_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | yes (count 8) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Lazlo/Ability/GA_Lazlo_Temperament.GA_Lazlo_Temperament_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | yes (count 1) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Proxima/Abilities/GA_Proxima_Biohunter.GA_Proxima_Biohunter_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Proxima/Abilities/GA_Proxima_Collector.GA_Proxima_Collector_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | yes (count 3) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Proxima/Abilities/GA_Proxima_Discharge.GA_Proxima_Discharge_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | none |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Proxima/Abilities/GA_Proxima_Energize.GA_Proxima_Energize_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | yes (count 1) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Proxima/Abilities/GA_Proxima_Reflection.GA_Proxima_Reflection_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | yes (count 7) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Proxima/Abilities/GA_Proxima_Seizure.GA_Proxima_Seizure_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | yes (count 4) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Proxima/Abilities/GA_Proxima_Shellshock.GA_Proxima_Shellshock_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | yes (count 1) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Smert/Ability/GA_Smert_Absolution.GA_Smert_Absolution_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 2) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Smert/Ability/GA_Smert_Blessing.GA_Smert_Blessing_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 2) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Smert/Ability/GA_Smert_Devotion.GA_Smert_Devotion_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 2) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Smert/Ability/GA_Smert_Limitless.GA_Smert_Limitless_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 3) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Smert/Ability/GA_Smert_PermanentFightStance.GA_Smert_PermanentFightStance_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 1) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Smert/Ability/GA_Smert_Resilience.GA_Smert_Resilience_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Thorn/Abilities/GA_Sariel_Affliction.GA_Sariel_Affliction_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | none |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Thorn/Abilities/GA_Sariel_Agonize.GA_Sariel_Agonize_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | yes (count 3) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Thorn/Abilities/GA_Sariel_Entitlement.GA_Sariel_Entitlement_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | yes (count 1) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Thorn/Abilities/GA_Sariel_Growth.GA_Sariel_Growth_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | none |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Thorn/Abilities/GA_Sariel_Restoration.GA_Sariel_Restoration_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | none |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Thorn/Abilities/GA_Sariel_Reversal.GA_Sariel_Reversal_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | none |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Tiel/Abilities/GA_Tiel_CriticalRole.GA_Tiel_CriticalRole_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | yes (count 4) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Tiel/Abilities/GA_Tiel_Deathmark.GA_Tiel_Deathmark_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellAbility_InflictEffect.GA_ShellAbility_InflictEffect_C` | none |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Tiel/Abilities/GA_Tiel_Escalation.GA_Tiel_Escalation_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellAbility_InflictEffect.GA_ShellAbility_InflictEffect_C` | none |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Tiel/Abilities/GA_Tiel_Lethality.GA_Tiel_Lethality_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | yes (count 2) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Tiel/Abilities/GA_Tiel_PlagueBearer.GA_Tiel_PlagueBearer_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | yes (count 1) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Tiel/Abilities/GA_Tiel_PoisonBurst.GA_Tiel_PoisonBurst_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | yes (count 1) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Tiel/Abilities/GA_Tiel_SealAffinity.GA_Tiel_SealAffinity_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | yes (count 1) |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Tiel/Abilities/GA_Tiel_ShadowClone.GA_Tiel_ShadowClone_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | none |
| `/Game/Sparta/Core/Player/Upgrades/Shells/Tiel/Abilities/GA_Tiel_ShadowStalk.GA_Tiel_ShadowStalk_C` | `/Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C` | yes (count 3) |
| `/Game/Sparta/Core/Player/Upgrades/Sidearm/Passives/Fragile/GA_Sidearm_InflictFragile.GA_Sidearm_InflictFragile_C` | `/Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C` | yes (count 2) |
| `/Game/Sparta/Core/Player/Upgrades/Weapons/ParasiteGun/GA_ParasiteGun_Contagion.GA_ParasiteGun_Contagion_C` | `/Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C` | yes (count 4) |
| `/Game/Sparta/Core/Player/Upgrades/Weapons/ParasiteGun/GA_ParasiteGun_IncreasedShootingRate.GA_ParasiteGun_IncreasedShootingRate_C` | `/Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C` | none |
| `/Game/Sparta/Core/Player/Upgrades/Weapons/Trebuchaxe/GA_TrebuchaxeActivation_Tiltshot.GA_TrebuchaxeActivation_Tiltshot_C` | `/Game/Sparta/Core/Player/Ability/Sidearm/Trebuchaxe/GA_TrebuchaxeSecondaryActivation.GA_TrebuchaxeSecondaryActivation_C` | none |

Evidence - upgrade ability class rows:
```
[000001F7A75A3FA0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C [n: 779C3] [c: 00007FF46EEC3D38] [or: 000001F7A7126280] [sps: 00007FF46F03F938]
[000001F8393B7180] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Weapons/Trebuchaxe/GA_TrebuchaxeActivation_Tiltshot.GA_TrebuchaxeActivation_Tiltshot_C [n: 36EE80] [c: 00007FF46EEC3D38] [or: 000001F831067E50] [sps: 000001F8382986E0]
[000001F8CDF07180] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Melee/Passives/Perforation/GA_Melee_InflictPerforation.GA_Melee_InflictPerforation_C [n: 492735] [c: 00007FF46EEC3D38] [or: 000001F7ED34DA30] [sps: 000001F7A75A3FA0]
[000001F8CDF05890] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Seals/GA_SlayerSeal_Attack.GA_SlayerSeal_Attack_C [n: 372F1E] [c: 00007FF46EEC3D38] [or: 000001F7ED34E6B0] [sps: 000001F4BBD2F550]
[000001F8CDF04A50] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Seals/GA_SlayerSeal_Charge.GA_SlayerSeal_Charge_C [n: 1F5753] [c: 00007FF46EEC3D38] [or: 000001F4CBCFF6F0] [sps: 000001F4DC018E00]
[000001F8CDF03C10] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Seals/GA_SlayerSeal_Handler.GA_SlayerSeal_Handler_C [n: 3CBA3C] [c: 00007FF46EEC3D38] [or: 000001F7ED34EDE0] [sps: 000001F7A75A3FA0]
[000001F8CDF02320] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C [n: 372F2A] [c: 00007FF46EEC3D38] [or: 000001F7ED34FCE0] [sps: 000001F4DC018E00]
[000001F8CDF01870] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/Abilities/GA_Eredrim_Alacrity.GA_Eredrim_Alacrity_C [n: 661D5] [c: 00007FF46EEC3D38] [or: 000001F7ED34FD80] [sps: 000001F8CDF02320]
[000001F8CDF00DC0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/Abilities/GA_Eredrim_Anguish.GA_Eredrim_Anguish_C [n: 33E963] [c: 00007FF46EEC3D38] [or: 000001F8C80F0A10] [sps: 000001F8CDF02320]
[000001F8CDF00310] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/Abilities/GA_Eredrim_Carnage.GA_Eredrim_Carnage_C [n: 172E14] [c: 00007FF46EEC3D38] [or: 000001F8C80F0970] [sps: 000001F8CDF02320]
[000001F8CDF0DC60] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/Abilities/GA_Eredrim_Consecration.GA_Eredrim_Consecration_C [n: 43B3CA] [c: 00007FF46EEC3D38] [or: 000001F8C80F08D0] [sps: 000001F8CDF02320]
[000001F8CDF0D1B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/Abilities/GA_Eredrim_Massacre.GA_Eredrim_Massacre_C [n: 3E16A0] [c: 00007FF46EEC3D38] [or: 000001F8C80F0790] [sps: 000001F8CDF02320]
[000001F8CDF0C700] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/Abilities/GA_Eredrim_Oppression.GA_Eredrim_Oppression_C [n: 1ECDC0] [c: 00007FF46EEC3D38] [or: 000001F8C80F06A0] [sps: 000001F8CDF02320]
[000001F8CDF0BC50] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/Abilities/GA_Eredrim_Resonance.GA_Eredrim_Resonance_C [n: 2F97C0] [c: 00007FF46EEC3D38] [or: 000001F8C80F0600] [sps: 000001F8CDF02320]
[000001F8CDF0B1A0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/Abilities/GA_Eredrim_SealAffinity.GA_Eredrim_SealAffinity_C [n: 25D9D5] [c: 00007FF46EEC3D38] [or: 000001F8C80F0510] [sps: 000001F8CDF02320]
[000001F8C81334F0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Generic/GA_ShellAbility_InflictEffect.GA_ShellAbility_InflictEffect_C [n: 1D688D] [c: 00007FF46EEC3D38] [or: 000001F8C80F1960] [sps: 000001F4DC018E00]
[000001F8C8130310] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Genessa/Abilities/GA_Genessa_Affinity.GA_Genessa_Affinity_C [n: 48DB6A] [c: 00007FF46EEC3D38] [or: 000001F8C80F2630] [sps: 000001F8CDF02320]
[000001F8C813DC60] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Genessa/Abilities/GA_Genessa_Alteration.GA_Genessa_Alteration_C [n: 4968C8] [c: 00007FF46EEC3D38] [or: 000001F8C80F24F0] [sps: 000001F8CDF02320]
[000001F8C813D1B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Genessa/Abilities/GA_Genessa_Augmentation.GA_Genessa_Augmentation_C [n: 132E40] [c: 00007FF46EEC3D38] [or: 000001F8C80F2450] [sps: 000001F8CDF02320]
[000001F8C813C700] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Genessa/Abilities/GA_Genessa_Causality.GA_Genessa_Causality_C [n: 107464] [c: 00007FF46EEC3D38] [or: 000001F8C80F23B0] [sps: 000001F8CDF02320]
[000001F8C813B530] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Genessa/Abilities/GA_Genessa_Communion.GA_Genessa_Communion_C [n: 183D91] [c: 00007FF46EEC3D38] [or: 000001F8C80F22C0] [sps: 000001F8CDF02320]
[000001F8C813A6F0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Genessa/Abilities/GA_Genessa_Distraction_AI.GA_Genessa_Distraction_AI_C [n: 33E96E] [c: 00007FF46EEC3D38] [or: 000001F8C80F21D0] [sps: 000001F8CDF02320]
[000001F8C81398B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Genessa/Abilities/GA_Genessa_Distraction.GA_Genessa_Distraction_C [n: 425482] [c: 00007FF46EEC3D38] [or: 000001F8C80F2180] [sps: 000001F8CDF02320]
[000001F8C8138E00] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Genessa/Abilities/GA_Genessa_Instigation.GA_Genessa_Instigation_C [n: 2DF3E5] [c: 00007FF46EEC3D38] [or: 000001F8C80F1F50] [sps: 000001F8CDF02320]
[000001F8C8138350] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Genessa/Abilities/GA_Genessa_Intangible.GA_Genessa_Intangible_C [n: 9ACA1] [c: 00007FF46EEC3D38] [or: 000001F8C80F32B0] [sps: 000001F8CDF02320]
[000001F8C813FC70] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Genessa/Abilities/GA_Genessa_Mirage.GA_Genessa_Mirage_C [n: 2B461D] [c: 00007FF46EEC3D38] [or: 000001F8C80F3120] [sps: 000001F8CDF02320]
[000001F8C8193FA0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Genessa/Abilities/GA_Genessa_Timeless.GA_Genessa_Timeless_C [n: 30FFED] [c: 00007FF46EEC3D38] [or: 000001F8C80F2F40] [sps: 000001F8CDF02320]
[000001F8C819B530] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Gragu/Abilities/GA_Gragu_Anger.GA_Gragu_Anger_C [n: 27BE12] [c: 00007FF46EEC3D38] [or: 000001F8C80F4C00] [sps: 000001F8C81334F0]
[000001F8C8199FD0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Gragu/Abilities/GA_Gragu_Bloodlust.GA_Gragu_Bloodlust_C [n: 1ECDCD] [c: 00007FF46EEC3D38] [or: 000001F8C80F4B60] [sps: 000001F4DC018E00]
[000001F8C8198A70] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Gragu/Abilities/GA_Gragu_Devourer.GA_Gragu_Devourer_C [n: 4550C5] [c: 00007FF46EEC3D38] [or: 000001F8C80F49D0] [sps: 000001F4DC018E00]
[000001F8C8197FC0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Gragu/Abilities/GA_Gragu_Fortify.GA_Gragu_Fortify_C [n: 1A2ED4] [c: 00007FF46EEC3D38] [or: 000001F8C80F48E0] [sps: 000001F8CDF02320]
[000001F8C819F8E0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Gragu/Abilities/GA_Gragu_Heartless_Handler.GA_Gragu_Heartless_Handler_C [n: 384486] [c: 00007FF46EEC3D38] [or: 000001F8C80F5C40] [sps: 000001F615242DD0]
[000001F8C819FC70] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Gragu/Abilities/GA_Gragu_Heartwork.GA_Gragu_Heartwork_C [n: 346E5A] [c: 00007FF46EEC3D38] [or: 000001F8C80F5AB0] [sps: 000001F8CDF02320]
[000001F8C81A6DF0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Gragu/Abilities/GA_Gragu_Hunger.GA_Gragu_Hunger_C [n: 405EC3] [c: 00007FF46EEC3D38] [or: 000001F8C80F5A10] [sps: 000001F8CDF02320]
[000001F8C81AD1B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Lazlo/Ability/GA_Lazlo_Detonation.GA_Lazlo_Detonation_C [n: 77A5B] [c: 00007FF46EEC3D38] [or: 000001F8C80F6F00] [sps: 000001F8CDF02320]
[000001F8C81AC700] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Lazlo/Ability/GA_Lazlo_Furnace.GA_Lazlo_Furnace_C [n: 8D85A] [c: 00007FF46EEC3D38] [or: 000001F8C80F67D0] [sps: 000001F8CDF02320]
[000001F8C81ABC50] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Lazlo/Ability/GA_Lazlo_InnerFire.GA_Lazlo_InnerFire_C [n: 1C9A22] [c: 00007FF46EEC3D38] [or: 000001F8C80F7A90] [sps: 000001F8CDF02320]
[000001F8C81AB1A0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Lazlo/Ability/GA_Lazlo_Rekindle.GA_Lazlo_Rekindle_C [n: AC2D1] [c: 00007FF46EEC3D38] [or: 000001F8C80F7950] [sps: 000001F8CDF02320]
[000001F8C81AA6F0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Lazlo/Ability/GA_Lazlo_Temperament.GA_Lazlo_Temperament_C [n: 1A2EDE] [c: 00007FF46EEC3D38] [or: 000001F8C80F7770] [sps: 000001F8CDF02320]
[000001F8D88F26B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Proxima/Abilities/GA_Proxima_Biohunter.GA_Proxima_Biohunter_C [n: 1ECDE5] [c: 00007FF46EEC3D38] [or: 000001F8C80F97A0] [sps: 000001F4DC018E00]
[000001F8D88F1F90] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Proxima/Abilities/GA_Proxima_Collector.GA_Proxima_Collector_C [n: 33A49F] [c: 00007FF46EEC3D38] [or: 000001F8C80F96B0] [sps: 000001F8CDF02320]
[000001F8D88F1150] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Proxima/Abilities/GA_Proxima_Discharge.GA_Proxima_Discharge_C [n: C6B0A] [c: 00007FF46EEC3D38] [or: 000001F8C80F95C0] [sps: 000001F8CDF02320]
[000001F8D88FE710] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Proxima/Abilities/GA_Proxima_Energize.GA_Proxima_Energize_C [n: 2547DB] [c: 00007FF46EEC3D38] [or: 000001F8C80F94D0] [sps: 000001F8CDF02320]
[000001F8D88FD540] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Proxima/Abilities/GA_Proxima_Reflection.GA_Proxima_Reflection_C [n: 13723C] [c: 00007FF46EEC3D38] [or: 000001F8C80F9390] [sps: 000001F8CDF02320]
[000001F8D88FCA90] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Proxima/Abilities/GA_Proxima_Seizure.GA_Proxima_Seizure_C [n: E9846] [c: 00007FF46EEC3D38] [or: 000001F8C80F9250] [sps: 000001F8CDF02320]
[000001F8D88FC370] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Proxima/Abilities/GA_Proxima_Shellshock.GA_Proxima_Shellshock_C [n: 2E8318] [c: 00007FF46EEC3D38] [or: 000001F8C80F90C0] [sps: 000001F8CDF02320]
[000001F8D89746C0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Smert/Ability/GA_Smert_Absolution.GA_Smert_Absolution_C [n: E073C] [c: 00007FF46EEC3D38] [or: 000001F8C80FA6A0] [sps: 000001F4DC018E00]
[000001F8D8973C10] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Smert/Ability/GA_Smert_Blessing.GA_Smert_Blessing_C [n: 1B49B1] [c: 00007FF46EEC3D38] [or: 000001F8C80FB550] [sps: 000001F4DC018E00]
[000001F8D8972DD0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Smert/Ability/GA_Smert_Devotion.GA_Smert_Devotion_C [n: 148481] [c: 00007FF46EEC3D38] [or: 000001F8C80FB4B0] [sps: 000001F4DC018E00]
[000001F8D8971F90] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Smert/Ability/GA_Smert_Limitless.GA_Smert_Limitless_C [n: 3C782C] [c: 00007FF46EEC3D38] [or: 000001F8C80FB3C0] [sps: 000001F4DC018E00]
[000001F8D8970DC0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Smert/Ability/GA_Smert_PermanentFightStance.GA_Smert_PermanentFightStance_C [n: 48DB8C] [c: 00007FF46EEC3D38] [or: 000001F8C80FB320] [sps: 000001F4DC018E00]
[000001F8D8970310] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Smert/Ability/GA_Smert_Resilience.GA_Smert_Resilience_C [n: 42548F] [c: 00007FF46EEC3D38] [or: 000001F8C80FB230] [sps: 000001F4DC018E00]
[000001F8D89D5890] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Thorn/Abilities/GA_Sariel_Affliction.GA_Sariel_Affliction_C [n: 28027F] [c: 00007FF46EEC3D38] [or: 000001F7ED346BE0] [sps: 000001F8CDF02320]
[000001F8D89D4A50] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Thorn/Abilities/GA_Sariel_Agonize.GA_Sariel_Agonize_C [n: 2F9834] [c: 00007FF46EEC3D38] [or: 000001F7ED346F50] [sps: 000001F8CDF02320]
[000001F8D89D4DE0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Thorn/Abilities/GA_Sariel_Entitlement.GA_Sariel_Entitlement_C [n: 4296F9] [c: 00007FF46EEC3D38] [or: 000001F7ED345D80] [sps: 000001F8CDF02320]
[000001F8D89D3880] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Thorn/Abilities/GA_Sariel_Growth.GA_Sariel_Growth_C [n: 24339F] [c: 00007FF46EEC3D38] [or: 000001F7ED346320] [sps: 000001F8CDF02320]
[000001F8D89D2DD0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Thorn/Abilities/GA_Sariel_Restoration.GA_Sariel_Restoration_C [n: 17F676] [c: 00007FF46EEC3D38] [or: 000001F8D760F740] [sps: 000001F8CDF02320]
[000001F8D89D1F90] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Thorn/Abilities/GA_Sariel_Reversal.GA_Sariel_Reversal_C [n: 3844CD] [c: 00007FF46EEC3D38] [or: 000001F8CF1BF060] [sps: 000001F8CDF02320]
[000001F8D89D8350] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Tiel/Abilities/GA_Tiel_CriticalRole.GA_Tiel_CriticalRole_C [n: E0748] [c: 00007FF46EEC3D38] [or: 000001F8C80FE070] [sps: 000001F8CDF02320]
[000001F8D89D7C30] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Tiel/Abilities/GA_Tiel_Deathmark.GA_Tiel_Deathmark_C [n: 49B151] [c: 00007FF46EEC3D38] [or: 000001F8C80FDFD0] [sps: 000001F8C81334F0]
[000001F8D89DF8E0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Tiel/Abilities/GA_Tiel_Escalation.GA_Tiel_Escalation_C [n: 2B8A73] [c: 00007FF46EEC3D38] [or: 000001F8C80FDF80] [sps: 000001F8C81334F0]
[000001F8D8A16DF0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Tiel/Abilities/GA_Tiel_Lethality.GA_Tiel_Lethality_C [n: 32590A] [c: 00007FF46EEC3D38] [or: 000001F8C80FDEE0] [sps: 000001F8CDF02320]
[000001F8D8A16340] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Tiel/Abilities/GA_Tiel_PlagueBearer.GA_Tiel_PlagueBearer_C [n: 14C67F] [c: 00007FF46EEC3D38] [or: 000001F8C80FDDA0] [sps: 000001F8CDF02320]
[000001F8D8A15890] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Tiel/Abilities/GA_Tiel_PoisonBurst.GA_Tiel_PoisonBurst_C [n: 1DF363] [c: 00007FF46EEC3D38] [or: 000001F8C80FDCB0] [sps: 000001F8CDF02320]
[000001F8D8A14330] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Tiel/Abilities/GA_Tiel_SealAffinity.GA_Tiel_SealAffinity_C [n: 23EEA2] [c: 00007FF46EEC3D38] [or: 000001F8C80FDB70] [sps: 000001F8CDF02320]
[000001F8D8A134F0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Tiel/Abilities/GA_Tiel_ShadowClone.GA_Tiel_ShadowClone_C [n: 3F3AA9] [c: 00007FF46EEC3D38] [or: 000001F8C80FDA30] [sps: 000001F8CDF02320]
[000001F8D8A11F90] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Tiel/Abilities/GA_Tiel_ShadowStalk.GA_Tiel_ShadowStalk_C [n: 18CFC9] [c: 00007FF46EEC3D38] [or: 000001F8C80FD9E0] [sps: 000001F8CDF02320]
[000001F8D8A5DC60] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Sidearm/Passives/Fragile/GA_Sidearm_InflictFragile.GA_Sidearm_InflictFragile_C [n: 450A63] [c: 00007FF46EEC3D38] [or: 000001F8D8A61410] [sps: 000001F7A75A3FA0]
[000001F8D8A5A6F0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Weapons/ParasiteGun/GA_ParasiteGun_Contagion.GA_ParasiteGun_Contagion_C [n: D3388] [c: 00007FF46EEC3D38] [or: 000001F8D8A61E60] [sps: 000001F7A75A3FA0]
[000001F8D8A59C40] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Weapons/ParasiteGun/GA_ParasiteGun_IncreasedShootingRate.GA_ParasiteGun_IncreasedShootingRate_C [n: 12E67B] [c: 00007FF46EEC3D38] [or: 000001F8D8A61D70] [sps: 000001F7A75A3FA0]
```

Evidence - GA_UpgradeBase properties:
```
[000001F7A75A3FA0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C [n: 779C3] [c: 00007FF46EEC3D38] [or: 000001F7A7126280] [sps: 00007FF46F03F938]
[000001F7B6E07980] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:UberGraphFrame [o: 508] [n: 59AB38] [c: 00007FF721DF9890] [owr: 000001F7A75A3FA0] [ss: 00007FF46F446958]
[000001F78B548120] NameProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:UpgradeID [o: 510] [n: 7516E2] [c: 00007FF721DE90B0] [owr: 000001F7A75A3FA0]
[000001F7B6E07400] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:PlayerCharacter [o: 518] [n: 58A6E5] [c: 00007FF721DE9420] [owr: 000001F7A75A3FA0] [pc: 00007FF46EE52028]
[000001F7B6E09480] SoftClassProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:ItemDef [o: 520] [n: 525597] [c: 00007FF721DF97A0] [owr: 000001F7A75A3FA0] [mc: 00007FF46F067338]
[000001F7A74C8F00] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeOrUnarmedAttack [n: 75160A] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F7B6E09E80] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeOrUnarmedAttack:Handle [o: 0] [n: 4E7240] [c: 00007FF721DF9890] [owr: 000001F7A74C8F00] [ss: 00007FF46F388BC0]
[000001F7B6E09E00] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeOrUnarmedAttack:ReturnValue [o: 18] [n: 4CF54F] [c: 00007FF721DE8D80] [owr: 000001F7A74C8F00]
[000001F7B6E09D80] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeOrUnarmedAttack:CallFunc_GetAbilityCDOFromContextHandle_ReturnValue [o: 20] [n: 636A12] [c: 00007FF721DE9420] [owr: 000001F7A74C8F00] [pc: 00007FF46EF91D38]
[000001F7B6E09D00] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeOrUnarmedAttack:CallFunc_GetAbilityCDOFromContextHandle_ReturnValue_1 [o: 28] [n: 636A12] [c: 00007FF721DE9420] [owr: 000001F7A74C8F00] [pc: 00007FF46EF91D38]
[000001F7B6E09C80] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeOrUnarmedAttack:CallFunc_GetAbilityTags_ReturnValue [o: 30] [n: 63750B] [c: 00007FF721DF9890] [owr: 000001F7A74C8F00] [ss: 00007FF46F388FE8]
[000001F7B6E09C00] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeOrUnarmedAttack:CallFunc_GetAbilityTags_ReturnValue_1 [o: 50] [n: 63750B] [c: 00007FF721DF9890] [owr: 000001F7A74C8F00] [ss: 00007FF46F388FE8]
[000001F7B6E09B80] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeOrUnarmedAttack:CallFunc_HasTag_ReturnValue [o: 70] [n: 621561] [c: 00007FF721DE8D80] [owr: 000001F7A74C8F00]
[000001F7B6E09B00] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeOrUnarmedAttack:CallFunc_HasTag_ReturnValue_1 [o: 71] [n: 621561] [c: 00007FF721DE8D80] [owr: 000001F7A74C8F00]
[000001F7B6E09A80] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeOrUnarmedAttack:CallFunc_BooleanOR_ReturnValue [o: 72] [n: 59AD69] [c: 00007FF721DE8D80] [owr: 000001F7A74C8F00]
[000001F7B6E09A00] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeOrUnarmedAttack:CallFunc_GetAbilityCDOFromContextHandle_ReturnValue_2 [o: 78] [n: 636A12] [c: 00007FF721DE9420] [owr: 000001F7A74C8F00] [pc: 00007FF46EF91D38]
[000001F7B6E09980] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeOrUnarmedAttack:CallFunc_GetAbilityTags_ReturnValue_2 [o: 80] [n: 63750B] [c: 00007FF721DF9890] [owr: 000001F7A74C8F00] [ss: 00007FF46F388FE8]
[000001F7B6E09900] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeOrUnarmedAttack:CallFunc_HasTag_ReturnValue_2 [o: A0] [n: 621561] [c: 00007FF721DE8D80] [owr: 000001F7A74C8F00]
[000001F7B6E09880] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeOrUnarmedAttack:CallFunc_BooleanOR_ReturnValue_1 [o: A1] [n: 59AD69] [c: 00007FF721DE8D80] [owr: 000001F7A74C8F00]
[000001F7A74C8E00] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeWeaponAbilityAttack [n: 751616] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F7B6E09800] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeWeaponAbilityAttack:Handle [o: 0] [n: 4E7240] [c: 00007FF721DF9890] [owr: 000001F7A74C8E00] [ss: 00007FF46F388BC0]
[000001F7B6E09780] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeWeaponAbilityAttack:ReturnValue [o: 18] [n: 4CF54F] [c: 00007FF721DE8D80] [owr: 000001F7A74C8E00]
[000001F7B6E09700] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeWeaponAbilityAttack:CallFunc_GetAbilityCDOFromContextHandle_ReturnValue [o: 20] [n: 636A12] [c: 00007FF721DE9420] [owr: 000001F7A74C8E00] [pc: 00007FF46EF91D38]
[000001F7B6E09680] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeWeaponAbilityAttack:CallFunc_GetAbilityTags_ReturnValue [o: 28] [n: 63750B] [c: 00007FF721DF9890] [owr: 000001F7A74C8E00] [ss: 00007FF46F388FE8]
[000001F7B6E09600] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeWeaponAbilityAttack:CallFunc_HasTag_ReturnValue [o: 48] [n: 621561] [c: 00007FF721DE8D80] [owr: 000001F7A74C8E00]
[000001F7A74C9700] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetItemDef [n: 4BD65] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F7B6E08E00] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetItemDef:CallFunc_GetCurrentSourceObject_ReturnValue [o: 0] [n: 75155E] [c: 00007FF721DE9420] [owr: 000001F7A74C9700] [pc: 00007FF46EE50070]
[000001F7B6E08D80] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetItemDef:K2Node_DynamicCast_AsSparta_Item_Instance [o: 8] [n: 75165F] [c: 00007FF721DE9420] [owr: 000001F7A74C9700] [pc: 00007FF46F067738]
[000001F7B6E08D00] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetItemDef:K2Node_DynamicCast_bSuccess [o: 10] [n: 59AAC7] [c: 00007FF721DE8D80] [owr: 000001F7A74C9700]
[000001F7B6E08C80] ClassProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetItemDef:CallFunc_GetItemDef_ReturnValue [o: 18] [n: 637002] [c: 00007FF721DE8E20] [owr: 000001F7A74C9700] [mc: 00007FF46F067338]
[000001F7B6E08C00] SoftClassProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetItemDef:CallFunc_Conv_ClassToSoftClassReference_ReturnValue [o: 20] [n: 62AF85] [c: 00007FF721DF97A0] [owr: 000001F7A74C9700] [mc: 00007FF46F067338]
[000001F7A74C8C00] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeWeaponHeavyHoldAttack [n: 751624] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F7B6E0B300] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeWeaponHeavyHoldAttack:Handle [o: 0] [n: 4E7240] [c: 00007FF721DF9890] [owr: 000001F7A74C8C00] [ss: 00007FF46F388BC0]
[000001F7B6E0B280] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeWeaponHeavyHoldAttack:ReturnValue [o: 18] [n: 4CF54F] [c: 00007FF721DE8D80] [owr: 000001F7A74C8C00]
[000001F7B6E0B200] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeWeaponHeavyHoldAttack:L_Tags [o: 20] [n: 65125B] [c: 00007FF721DF9890] [owr: 000001F7A74C8C00] [ss: 00007FF46F388FE8]
[000001F7B6E0B180] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeWeaponHeavyHoldAttack:CallFunc_GetAbilityCDOFromContextHandle_ReturnValue [o: 40] [n: 636A12] [c: 00007FF721DE9420] [owr: 000001F7A74C8C00] [pc: 00007FF46EF91D38]
[000001F7B6E0B100] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeWeaponHeavyHoldAttack:CallFunc_GetAbilityTags_ReturnValue [o: 48] [n: 63750B] [c: 00007FF721DF9890] [owr: 000001F7A74C8C00] [ss: 00007FF46F388FE8]
[000001F7B6E0B080] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeWeaponHeavyHoldAttack:CallFunc_HasTag_ReturnValue [o: 68] [n: 621561] [c: 00007FF721DE8D80] [owr: 000001F7A74C8C00]
[000001F7B6E0B000] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeWeaponHeavyHoldAttack:CallFunc_HasTag_ReturnValue_1 [o: 69] [n: 621561] [c: 00007FF721DE8D80] [owr: 000001F7A74C8C00]
[000001F7A74C8B00] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeWeaponLightHoldAttack [n: 751633] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F7B6E0AF80] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeWeaponLightHoldAttack:Handle [o: 0] [n: 4E7240] [c: 00007FF721DF9890] [owr: 000001F7A74C8B00] [ss: 00007FF46F388BC0]
[000001F7B6E0AF00] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeWeaponLightHoldAttack:ReturnValue [o: 18] [n: 4CF54F] [c: 00007FF721DE8D80] [owr: 000001F7A74C8B00]
[000001F7B6E0AE80] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeWeaponLightHoldAttack:L_Tags [o: 20] [n: 65125B] [c: 00007FF721DF9890] [owr: 000001F7A74C8B00] [ss: 00007FF46F388FE8]
[000001F7B6E0AE00] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeWeaponLightHoldAttack:CallFunc_GetAbilityCDOFromContextHandle_ReturnValue [o: 40] [n: 636A12] [c: 00007FF721DE9420] [owr: 000001F7A74C8B00] [pc: 00007FF46EF91D38]
[000001F7B6E0AD80] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeWeaponLightHoldAttack:CallFunc_GetAbilityTags_ReturnValue [o: 48] [n: 63750B] [c: 00007FF721DF9890] [owr: 000001F7A74C8B00] [ss: 00007FF46F388FE8]
[000001F7B6E0AD00] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeWeaponLightHoldAttack:CallFunc_HasTag_ReturnValue [o: 68] [n: 621561] [c: 00007FF721DE8D80] [owr: 000001F7A74C8B00]
[000001F7B6E0AC80] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeWeaponLightHoldAttack:CallFunc_HasTag_ReturnValue_1 [o: 69] [n: 621561] [c: 00007FF721DE8D80] [owr: 000001F7A74C8B00]
[000001F7A74C9100] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsAnyMeleeWeaponHoldAttack [n: 7515FC] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F7B6E08580] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsAnyMeleeWeaponHoldAttack:Handle [o: 0] [n: 4E7240] [c: 00007FF721DF9890] [owr: 000001F7A74C9100] [ss: 00007FF46F388BC0]
[000001F7B6E08500] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsAnyMeleeWeaponHoldAttack:ReturnValue [o: 18] [n: 4CF54F] [c: 00007FF721DE8D80] [owr: 000001F7A74C9100]
[000001F7B6E08480] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsAnyMeleeWeaponHoldAttack:L_Tags [o: 20] [n: 65125B] [c: 00007FF721DF9890] [owr: 000001F7A74C9100] [ss: 00007FF46F388FE8]
[000001F7B6E0A500] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsAnyMeleeWeaponHoldAttack:CallFunc_GetAbilityCDOFromContextHandle_ReturnValue [o: 40] [n: 636A12] [c: 00007FF721DE9420] [owr: 000001F7A74C9100] [pc: 00007FF46EF91D38]
[000001F7B6E0A480] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsAnyMeleeWeaponHoldAttack:CallFunc_GetAbilityTags_ReturnValue [o: 48] [n: 63750B] [c: 00007FF721DF9890] [owr: 000001F7A74C9100] [ss: 00007FF46F388FE8]
[000001F7B6E0A400] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsAnyMeleeWeaponHoldAttack:CallFunc_HasTag_ReturnValue [o: 68] [n: 621561] [c: 00007FF721DE8D80] [owr: 000001F7A74C9100]
[000001F7B6E0A380] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsAnyMeleeWeaponHoldAttack:CallFunc_HasTag_ReturnValue_1 [o: 69] [n: 621561] [c: 00007FF721DE8D80] [owr: 000001F7A74C9100]
[000001F7B6E0A300] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsAnyMeleeWeaponHoldAttack:CallFunc_HasTag_ReturnValue_2 [o: 6A] [n: 621561] [c: 00007FF721DE8D80] [owr: 000001F7A74C9100]
[000001F7B6E0A280] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsAnyMeleeWeaponHoldAttack:CallFunc_BooleanOR_ReturnValue [o: 6B] [n: 59AD69] [c: 00007FF721DE8D80] [owr: 000001F7A74C9100]
[000001F7A74C8A00] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsNormalMeleeWeaponAttack [n: 751642] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F7B6E0AC00] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsNormalMeleeWeaponAttack:Handle [o: 0] [n: 4E7240] [c: 00007FF721DF9890] [owr: 000001F7A74C8A00] [ss: 00007FF46F388BC0]
[000001F7B6E0AB80] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsNormalMeleeWeaponAttack:ReturnValue [o: 18] [n: 4CF54F] [c: 00007FF721DE8D80] [owr: 000001F7A74C8A00]
[000001F7B6E0AB00] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsNormalMeleeWeaponAttack:CallFunc_GetSpartaAIControllerFromActorInfo_ReturnValue [o: 20] [n: 625317] [c: 00007FF721DE9420] [owr: 000001F7A74C8A00] [pc: 00007FF46F03C738]
[000001F7B6E0AA80] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsNormalMeleeWeaponAttack:CallFunc_GetAbilityCDOFromContextHandle_ReturnValue [o: 28] [n: 636A12] [c: 00007FF721DE9420] [owr: 000001F7A74C8A00] [pc: 00007FF46EF91D38]
[000001F7B6E0AA00] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsNormalMeleeWeaponAttack:CallFunc_IsValid_ReturnValue [o: 30] [n: 59AA4C] [c: 00007FF721DE8D80] [owr: 000001F7A74C8A00]
[000001F7B6E0A980] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsNormalMeleeWeaponAttack:CallFunc_GetAbilityTags_ReturnValue [o: 38] [n: 63750B] [c: 00007FF721DF9890] [owr: 000001F7A74C8A00] [ss: 00007FF46F388FE8]
[000001F7B6E0A900] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsNormalMeleeWeaponAttack:CallFunc_HasTag_ReturnValue [o: 58] [n: 621561] [c: 00007FF721DE8D80] [owr: 000001F7A74C8A00]
[000001F7B6E0A880] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsNormalMeleeWeaponAttack:CallFunc_BooleanOR_ReturnValue [o: 59] [n: 59AD69] [c: 00007FF721DE8D80] [owr: 000001F7A74C8A00]
[000001F7A74C9000] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsAnySidearmWeaponAttack [n: 75136E] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F7B6E0A200] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsAnySidearmWeaponAttack:Handle [o: 0] [n: 4E7240] [c: 00007FF721DF9890] [owr: 000001F7A74C9000] [ss: 00007FF46F388BC0]
[000001F7B6E0A180] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsAnySidearmWeaponAttack:ReturnValue [o: 18] [n: 4CF54F] [c: 00007FF721DE8D80] [owr: 000001F7A74C9000]
[000001F7B6E0A100] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsAnySidearmWeaponAttack:CallFunc_GetAbilityCDOFromContextHandle_ReturnValue [o: 20] [n: 636A12] [c: 00007FF721DE9420] [owr: 000001F7A74C9000] [pc: 00007FF46EF91D38]
[000001F7B6E0A080] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsAnySidearmWeaponAttack:CallFunc_GetAbilityTags_ReturnValue [o: 28] [n: 63750B] [c: 00007FF721DF9890] [owr: 000001F7A74C9000] [ss: 00007FF46F388FE8]
[000001F7B6E0A000] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsAnySidearmWeaponAttack:CallFunc_HasTag_ReturnValue [o: 48] [n: 621561] [c: 00007FF721DE8D80] [owr: 000001F7A74C9000]
[000001F7B6E09F80] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsAnySidearmWeaponAttack:CallFunc_HasTag_ReturnValue_1 [o: 49] [n: 621561] [c: 00007FF721DE8D80] [owr: 000001F7A74C9000]
[000001F7B6E09F00] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsAnySidearmWeaponAttack:CallFunc_BooleanOR_ReturnValue [o: 4A] [n: 59AD69] [c: 00007FF721DE8D80] [owr: 000001F7A74C9000]
[000001F7A74C8800] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsSidearmWeaponSecondaryAttack [n: 751387] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F7B6E0A580] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsSidearmWeaponSecondaryAttack:Handle [o: 0] [n: 4E7240] [c: 00007FF721DF9890] [owr: 000001F7A74C8800] [ss: 00007FF46F388BC0]
[000001F7B6E0C600] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsSidearmWeaponSecondaryAttack:ReturnValue [o: 18] [n: 4CF54F] [c: 00007FF721DE8D80] [owr: 000001F7A74C8800]
[000001F7B6E0C580] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsSidearmWeaponSecondaryAttack:CallFunc_GetAbilityCDOFromContextHandle_ReturnValue [o: 20] [n: 636A12] [c: 00007FF721DE9420] [owr: 000001F7A74C8800] [pc: 00007FF46EF91D38]
[000001F7B6E0C500] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsSidearmWeaponSecondaryAttack:CallFunc_GetAbilityTags_ReturnValue [o: 28] [n: 63750B] [c: 00007FF721DF9890] [owr: 000001F7A74C8800] [ss: 00007FF46F388FE8]
[000001F7B6E0C480] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsSidearmWeaponSecondaryAttack:CallFunc_HasTag_ReturnValue [o: 48] [n: 621561] [c: 00007FF721DE8D80] [owr: 000001F7A74C8800]
[000001F7A74C8900] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsSidearmWeaponPrimaryAttack [n: 751650] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F7B6E0A800] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsSidearmWeaponPrimaryAttack:Handle [o: 0] [n: 4E7240] [c: 00007FF721DF9890] [owr: 000001F7A74C8900] [ss: 00007FF46F388BC0]
[000001F7B6E0A780] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsSidearmWeaponPrimaryAttack:ReturnValue [o: 18] [n: 4CF54F] [c: 00007FF721DE8D80] [owr: 000001F7A74C8900]
[000001F7B6E0A700] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsSidearmWeaponPrimaryAttack:CallFunc_GetAbilityCDOFromContextHandle_ReturnValue [o: 20] [n: 636A12] [c: 00007FF721DE9420] [owr: 000001F7A74C8900] [pc: 00007FF46EF91D38]
[000001F7B6E0A680] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsSidearmWeaponPrimaryAttack:CallFunc_GetAbilityTags_ReturnValue [o: 28] [n: 63750B] [c: 00007FF721DF9890] [owr: 000001F7A74C8900] [ss: 00007FF46F388FE8]
[000001F7B6E0A600] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsSidearmWeaponPrimaryAttack:CallFunc_HasTag_ReturnValue [o: 48] [n: 621561] [c: 00007FF721DE8D80] [owr: 000001F7A74C8900]
[000001F7A74C8D00] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeWeaponAttack [n: 65923F] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F7B6E09580] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeWeaponAttack:Handle [o: 0] [n: 4E7240] [c: 00007FF721DF9890] [owr: 000001F7A74C8D00] [ss: 00007FF46F388BC0]
[000001F7B6E09500] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeWeaponAttack:ReturnValue [o: 18] [n: 4CF54F] [c: 00007FF721DE8D80] [owr: 000001F7A74C8D00]
[000001F7B6E0B580] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeWeaponAttack:CallFunc_GetAbilityCDOFromContextHandle_ReturnValue [o: 20] [n: 636A12] [c: 00007FF721DE9420] [owr: 000001F7A74C8D00] [pc: 00007FF46EF91D38]
[000001F7B6E0B500] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeWeaponAttack:CallFunc_GetAbilityTags_ReturnValue [o: 28] [n: 63750B] [c: 00007FF721DF9890] [owr: 000001F7A74C8D00] [ss: 00007FF46F388FE8]
[000001F7B6E0B480] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeWeaponAttack:CallFunc_HasTag_ReturnValue [o: 48] [n: 621561] [c: 00007FF721DE8D80] [owr: 000001F7A74C8D00]
[000001F7B6E0B400] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeWeaponAttack:CallFunc_HasTag_ReturnValue_1 [o: 49] [n: 621561] [c: 00007FF721DE8D80] [owr: 000001F7A74C8D00]
[000001F7B6E0B380] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:IsMeleeWeaponAttack:CallFunc_BooleanOR_ReturnValue [o: 4A] [n: 59AD69] [c: 00007FF721DE8D80] [owr: 000001F7A74C8D00]
[000001F7A74C9800] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetGEStackCountByTagFromActor [n: 7515B4] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F7B6E09180] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetGEStackCountByTagFromActor:Actor [o: 0] [n: 213] [c: 00007FF721DE9420] [owr: 000001F7A74C9800] [pc: 00007FF46EE52028]
[000001F7B6E09100] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetGEStackCountByTagFromActor:Tags [o: 8] [n: 4CBA34] [c: 00007FF721DF9890] [owr: 000001F7A74C9800] [ss: 00007FF46F388FE8]
[000001F78B547FD0] IntProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetGEStackCountByTagFromActor:ReturnValue [o: 28] [n: 4CF54F] [c: 00007FF721DE91F0] [owr: 000001F7A74C9800]
[000001F7B6E09080] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetGEStackCountByTagFromActor:CallFunc_GetAbilitySystemComponent_ReturnValue [o: 30] [n: 5BDA19] [c: 00007FF721DE9420] [owr: 000001F7A74C9800] [pc: 00007FF46EF98538]
[000001F7B6E09000] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetGEStackCountByTagFromActor:CallFunc_MakeLiteralGameplayTagContainer_ReturnValue [o: 38] [n: 620EF8] [c: 00007FF721DF9890] [owr: 000001F7A74C9800] [ss: 00007FF46F388FE8]
[000001F7B6E08F80] ArrayProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetGEStackCountByTagFromActor:CallFunc_GetActiveEffectsWithAllTags_ReturnValue [o: 58] [n: 63715F] [c: 00007FF721DE8910] [owr: 000001F7A74C9800] [ai: 000001F7B6E08F00]
[000001F7B6E08F00] StructProperty CallFunc_GetActiveEffectsWithAllTags_ReturnValue./Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetGEStackCountByTagFromActor:CallFunc_GetActiveEffectsWithAllTags_ReturnValue [o: 0] [n: 63715F] [c: 00007FF721DF9890] [owr: 000001F7B6E08F80] [ss: 00007FF46F389C70]
[000001F7B6E08E80] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetGEStackCountByTagFromActor:CallFunc_Array_Get_Item [o: 68] [n: 59AD39] [c: 00007FF721DF9890] [owr: 000001F7A74C9800] [ss: 00007FF46F389C70]
[000001F78B547F60] IntProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetGEStackCountByTagFromActor:CallFunc_GetActiveGameplayEffectStackCount_ReturnValue [o: 70] [n: 64C703] [c: 00007FF721DE91F0] [owr: 000001F7A74C9800]
[000001F7A74C9900] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetGameplayEffectStackCountFromActor [n: 7515A1] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F7B6E09380] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetGameplayEffectStackCountFromActor:Actor [o: 0] [n: 213] [c: 00007FF721DE9420] [owr: 000001F7A74C9900] [pc: 00007FF46EE52028]
[000001F7B6E09300] SoftClassProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetGameplayEffectStackCountFromActor:GameplayEffect [o: 8] [n: 3B2FC] [c: 00007FF721DF97A0] [owr: 000001F7A74C9900] [mc: 00007FF46EF8E138]
[000001F78B5480B0] IntProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetGameplayEffectStackCountFromActor:ReturnValue [o: 30] [n: 4CF54F] [c: 00007FF721DE91F0] [owr: 000001F7A74C9900]
[000001F7B6E09280] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetGameplayEffectStackCountFromActor:CallFunc_GetAbilitySystemComponent_ReturnValue [o: 38] [n: 5BDA19] [c: 00007FF721DE9420] [owr: 000001F7A74C9900] [pc: 00007FF46EF98538]
[000001F78B548040] IntProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetGameplayEffectStackCountFromActor:CallFunc_GetGameplayEffectCount_IfLoaded_ReturnValue [o: 40] [n: 751575] [c: 00007FF721DE91F0] [owr: 000001F7A74C9900]
[000001F7A74CC100] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:OwnerHasUpgrade [n: 75168A] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F7B6E0C300] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:OwnerHasUpgrade:ReturnValue [o: 0] [n: 4CF54F] [c: 00007FF721DE8D80] [owr: 000001F7A74CC100]
[000001F7B6E0C280] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:OwnerHasUpgrade:CallFunc_GetSpartaCharacterFromActorInfo_ReturnValue [o: 8] [n: 5FF5C1] [c: 00007FF721DE9420] [owr: 000001F7A74CC100] [pc: 00007FF46F03C538]
[000001F7B6E0C200] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:OwnerHasUpgrade:CallFunc_GetComponentByClass_ReturnValue [o: 10] [n: 5DB8EC] [c: 00007FF721DE9420] [owr: 000001F7A74CC100] [pc: 000001F606B67510]
[000001F7B6E0C180] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:OwnerHasUpgrade:CallFunc_HasUpgrade_ReturnValue [o: 18] [n: 650AA6] [c: 00007FF721DE8D80] [owr: 000001F7A74CC100]
[000001F7A74CC000] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:PlayerHasUpgrade [n: 751693] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F7B6E0C100] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:PlayerHasUpgrade:ReturnValue [o: 0] [n: 4CF54F] [c: 00007FF721DE8D80] [owr: 000001F7A74CC000]
[000001F7B6E0C080] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:PlayerHasUpgrade:CallFunc_IsValid_ReturnValue [o: 1] [n: 59AA4C] [c: 00007FF721DE8D80] [owr: 000001F7A74CC000]
[000001F7B6E0C000] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:PlayerHasUpgrade:CallFunc_GetComponentByClass_ReturnValue [o: 8] [n: 5DB8EC] [c: 00007FF721DE9420] [owr: 000001F7A74CC000] [pc: 000001F606B67510]
[000001F7B6E0BF80] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:PlayerHasUpgrade:CallFunc_HasUpgrade_ReturnValue [o: 10] [n: 650AA6] [c: 00007FF721DE8D80] [owr: 000001F7A74CC000]
[000001F7A74C9300] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetUpgradeStatValueFromPlayer [n: 7515DF] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F7B6E08800] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetUpgradeStatValueFromPlayer:StatTag [o: 0] [n: 55C6D8] [c: 00007FF721DF9890] [owr: 000001F7A74C9300] [ss: 00007FF46F3890A8]
[000001F78B547C50] FloatProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetUpgradeStatValueFromPlayer:ReturnValue [o: 8] [n: 4CF54F] [c: 00007FF721DE9380] [owr: 000001F7A74C9300]
[000001F7B6E08780] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetUpgradeStatValueFromPlayer:CallFunc_IsValid_ReturnValue [o: C] [n: 59AA4C] [c: 00007FF721DE8D80] [owr: 000001F7A74C9300]
[000001F7B6E08700] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetUpgradeStatValueFromPlayer:CallFunc_GetComponentByClass_ReturnValue [o: 10] [n: 5DB8EC] [c: 00007FF721DE9420] [owr: 000001F7A74C9300] [pc: 00007FF46F03B938]
[000001F78B547BE0] FloatProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetUpgradeStatValueFromPlayer:CallFunc_GetUpgradeStatValue_ReturnValue [o: 18] [n: 5FF5DC] [c: 00007FF721DE9380] [owr: 000001F7A74C9300]
[000001F7A74C9500] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetUpgradeLevelFromPlayer [n: 7515D1] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F78B547E10] IntProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetUpgradeLevelFromPlayer:ReturnValue [o: 0] [n: 4CF54F] [c: 00007FF721DE91F0] [owr: 000001F7A74C9500]
[000001F7B6E08A80] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetUpgradeLevelFromPlayer:CallFunc_IsValid_ReturnValue [o: 4] [n: 59AA4C] [c: 00007FF721DE8D80] [owr: 000001F7A74C9500]
[000001F7B6E08A00] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetUpgradeLevelFromPlayer:CallFunc_GetComponentByClass_ReturnValue [o: 8] [n: 5DB8EC] [c: 00007FF721DE9420] [owr: 000001F7A74C9500] [pc: 000001F606B67510]
[000001F78B547DA0] IntProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetUpgradeLevelFromPlayer:CallFunc_GetUpgradeLevel_ReturnValue [o: 10] [n: 650A5D] [c: 00007FF721DE91F0] [owr: 000001F7A74C9500]
[000001F7A74C9200] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:HandleChanceByBoolWeight [n: 7515EF] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F78B547B70] FloatProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:HandleChanceByBoolWeight:Weight [o: 0] [n: 6B8F] [c: 00007FF721DE9380] [owr: 000001F7A74C9200]
[000001F7B6E08680] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:HandleChanceByBoolWeight:ReturnValue [o: 4] [n: 4CF54F] [c: 00007FF721DE8D80] [owr: 000001F7A74C9200]
[000001F7B6E08600] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:HandleChanceByBoolWeight:CallFunc_RandomBoolWithWeight_ReturnValue [o: 5] [n: 5EFD86] [c: 00007FF721DE8D80] [owr: 000001F7A74C9200]
[000001F7A74C9400] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetUpgradeStatValueFromOwner [n: 751349] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F7B6E08980] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetUpgradeStatValueFromOwner:StatTag [o: 0] [n: 55C6D8] [c: 00007FF721DF9890] [owr: 000001F7A74C9400] [ss: 00007FF46F3890A8]
[000001F78B547D30] FloatProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetUpgradeStatValueFromOwner:ReturnValue [o: 8] [n: 4CF54F] [c: 00007FF721DE9380] [owr: 000001F7A74C9400]
[000001F7B6E08900] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetUpgradeStatValueFromOwner:CallFunc_GetSpartaAbilitySystemComponentFromActorInfo_ReturnValue [o: 10] [n: 654491] [c: 00007FF721DE9420] [owr: 000001F7A74C9400] [pc: 00007FF46F03B938]
[000001F78B547CC0] FloatProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetUpgradeStatValueFromOwner:CallFunc_GetUpgradeStatValue_ReturnValue [o: 18] [n: 5FF5DC] [c: 00007FF721DE9380] [owr: 000001F7A74C9400]
[000001F7A74C9600] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetUpgradeLevelFromOwner [n: 7515C4] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F78B547EF0] IntProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetUpgradeLevelFromOwner:ReturnValue [o: 0] [n: 4CF54F] [c: 00007FF721DE91F0] [owr: 000001F7A74C9600]
[000001F7B6E08B80] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetUpgradeLevelFromOwner:CallFunc_GetSpartaCharacterFromActorInfo_ReturnValue [o: 8] [n: 5FF5C1] [c: 00007FF721DE9420] [owr: 000001F7A74C9600] [pc: 00007FF46F03C538]
[000001F7B6E08B00] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetUpgradeLevelFromOwner:CallFunc_GetComponentByClass_ReturnValue [o: 10] [n: 5DB8EC] [c: 00007FF721DE9420] [owr: 000001F7A74C9600] [pc: 000001F606B67510]
[000001F78B547E80] IntProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:GetUpgradeLevelFromOwner:CallFunc_GetUpgradeLevel_ReturnValue [o: 18] [n: 650A5D] [c: 00007FF721DE91F0] [owr: 000001F7A74C9600]
[000001F7A74CB900] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityRemoved [n: 7516D4] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F7B6E0CD00] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityRemoved:Character [o: 0] [n: 30DE5] [c: 00007FF721DE9420] [owr: 000001F7A74CB900] [pc: 00007FF46F03C538]
[000001F7B6E0CC80] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityRemoved:CallFunc_IsValid_ReturnValue [o: 8] [n: 59AA4C] [c: 00007FF721DE8D80] [owr: 000001F7A74CB900]
[000001F7B6E0CC00] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityRemoved:CallFunc_GetController_ReturnValue [o: 10] [n: 61FF56] [c: 00007FF721DE9420] [owr: 000001F7A74CB900] [pc: 00007FF46EF37F38]
[000001F7B6E0CB80] InterfaceProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityRemoved:K2Node_DynamicCast_AsBPI_Generic_Character_Controller [o: 18] [n: 636D6E] [c: 00007FF721DE8ED0] [owr: 000001F7A74CB900] [ic: 000001F4DC019190]
[000001F7B6E0CB00] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityRemoved:K2Node_DynamicCast_bSuccess [o: 28] [n: 59AAC7] [c: 00007FF721DE8D80] [owr: 000001F7A74CB900]
[000001F7B6E0CA80] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityRemoved:CallFunc_IsPlayerCharacter_Return [o: 29] [n: 7280D1] [c: 00007FF721DE8D80] [owr: 000001F7A74CB900]
[000001F7B6E0CA00] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityRemoved:CallFunc_GetGameplayAbilityTracker_ReturnValue [o: 30] [n: 6373C6] [c: 00007FF721DE9420] [owr: 000001F7A74CB900] [pc: 000001F4DC019520]
[000001F7B6E0C980] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityRemoved:CallFunc_GetSpartaCharacterFromActorInfo_ReturnValue [o: 38] [n: 5FF5C1] [c: 00007FF721DE9420] [owr: 000001F7A74CB900] [pc: 00007FF46F03C538]
[000001F7A74CBA00] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityEnded [n: 7516C7] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F7B6E0D180] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityEnded:WasCancelled [o: 0] [n: 4FA3C1] [c: 00007FF721DE8D80] [owr: 000001F7A74CBA00]
[000001F7B6E0D100] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityEnded:Character [o: 8] [n: 30DE5] [c: 00007FF721DE9420] [owr: 000001F7A74CBA00] [pc: 00007FF46F03C538]
[000001F7B6E0D080] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityEnded:CallFunc_IsValid_ReturnValue [o: 10] [n: 59AA4C] [c: 00007FF721DE8D80] [owr: 000001F7A74CBA00]
[000001F7B6E0D000] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityEnded:CallFunc_GetController_ReturnValue [o: 18] [n: 61FF56] [c: 00007FF721DE9420] [owr: 000001F7A74CBA00] [pc: 00007FF46EF37F38]
[000001F7B6E0CF80] InterfaceProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityEnded:K2Node_DynamicCast_AsBPI_Generic_Character_Controller [o: 20] [n: 636D6E] [c: 00007FF721DE8ED0] [owr: 000001F7A74CBA00] [ic: 000001F4DC019190]
[000001F7B6E0CF00] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityEnded:K2Node_DynamicCast_bSuccess [o: 30] [n: 59AAC7] [c: 00007FF721DE8D80] [owr: 000001F7A74CBA00]
[000001F7B6E0CE80] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityEnded:CallFunc_IsPlayerCharacter_Return [o: 31] [n: 7280D1] [c: 00007FF721DE8D80] [owr: 000001F7A74CBA00]
[000001F7B6E0CE00] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityEnded:CallFunc_GetGameplayAbilityTracker_ReturnValue [o: 38] [n: 6373C6] [c: 00007FF721DE9420] [owr: 000001F7A74CBA00] [pc: 000001F4DC019520]
[000001F7B6E0CD80] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityEnded:CallFunc_GetSpartaCharacterFromActorInfo_ReturnValue [o: 40] [n: 5FF5C1] [c: 00007FF721DE9420] [owr: 000001F7A74CBA00] [pc: 00007FF46F03C538]
[000001F7A74CBB00] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityCommitted [n: 7516B8] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F7B6E0D580] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityCommitted:Character [o: 0] [n: 30DE5] [c: 00007FF721DE9420] [owr: 000001F7A74CBB00] [pc: 00007FF46F03C538]
[000001F7B6E0D500] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityCommitted:CallFunc_IsValid_ReturnValue [o: 8] [n: 59AA4C] [c: 00007FF721DE8D80] [owr: 000001F7A74CBB00]
[000001F7B6E0D480] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityCommitted:CallFunc_GetController_ReturnValue [o: 10] [n: 61FF56] [c: 00007FF721DE9420] [owr: 000001F7A74CBB00] [pc: 00007FF46EF37F38]
[000001F7B6E0D400] InterfaceProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityCommitted:K2Node_DynamicCast_AsBPI_Generic_Character_Controller [o: 18] [n: 636D6E] [c: 00007FF721DE8ED0] [owr: 000001F7A74CBB00] [ic: 000001F4DC019190]
[000001F7B6E0D380] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityCommitted:K2Node_DynamicCast_bSuccess [o: 28] [n: 59AAC7] [c: 00007FF721DE8D80] [owr: 000001F7A74CBB00]
[000001F7B6E0D300] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityCommitted:CallFunc_IsPlayerCharacter_Return [o: 29] [n: 7280D1] [c: 00007FF721DE8D80] [owr: 000001F7A74CBB00]
[000001F7B6E0D280] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityCommitted:CallFunc_GetGameplayAbilityTracker_ReturnValue [o: 30] [n: 6373C6] [c: 00007FF721DE9420] [owr: 000001F7A74CBB00] [pc: 000001F4DC019520]
[000001F7B6E0D200] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityCommitted:CallFunc_GetSpartaCharacterFromActorInfo_ReturnValue [o: 38] [n: 5FF5C1] [c: 00007FF721DE9420] [owr: 000001F7A74CBB00] [pc: 00007FF46F03C538]
[000001F7A74CBD00] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityActivated [n: 75169C] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F7B6E0BD80] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityActivated:Character [o: 0] [n: 30DE5] [c: 00007FF721DE9420] [owr: 000001F7A74CBD00] [pc: 00007FF46F03C538]
[000001F7B6E0BD00] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityActivated:CallFunc_IsValid_ReturnValue [o: 8] [n: 59AA4C] [c: 00007FF721DE8D80] [owr: 000001F7A74CBD00]
[000001F7B6E0BC80] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityActivated:CallFunc_GetController_ReturnValue [o: 10] [n: 61FF56] [c: 00007FF721DE9420] [owr: 000001F7A74CBD00] [pc: 00007FF46EF37F38]
[000001F7B6E0BC00] InterfaceProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityActivated:K2Node_DynamicCast_AsBPI_Generic_Character_Controller [o: 18] [n: 636D6E] [c: 00007FF721DE8ED0] [owr: 000001F7A74CBD00] [ic: 000001F4DC019190]
[000001F7B6E0BB80] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityActivated:K2Node_DynamicCast_bSuccess [o: 28] [n: 59AAC7] [c: 00007FF721DE8D80] [owr: 000001F7A74CBD00]
[000001F7B6E0BB00] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityActivated:CallFunc_IsPlayerCharacter_Return [o: 29] [n: 7280D1] [c: 00007FF721DE8D80] [owr: 000001F7A74CBD00]
[000001F7B6E0BA80] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityActivated:CallFunc_GetGameplayAbilityTracker_ReturnValue [o: 30] [n: 6373C6] [c: 00007FF721DE9420] [owr: 000001F7A74CBD00] [pc: 000001F4DC019520]
[000001F7B6E0BA00] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityActivated:CallFunc_GetSpartaCharacterFromActorInfo_ReturnValue [o: 38] [n: 5FF5C1] [c: 00007FF721DE9420] [owr: 000001F7A74CBD00] [pc: 00007FF46F03C538]
[000001F7A74CBC00] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityAdded [n: 7516AB] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F7B6E0B980] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityAdded:Controller [o: 0] [n: 3103A] [c: 00007FF721DE9420] [owr: 000001F7A74CBC00] [pc: 00007FF46EF37F38]
[000001F7B6E0B900] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityAdded:Character [o: 8] [n: 30DE5] [c: 00007FF721DE9420] [owr: 000001F7A74CBC00] [pc: 00007FF46F03C538]
[000001F7B6E0B880] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityAdded:CallFunc_IsValid_ReturnValue [o: 10] [n: 59AA4C] [c: 00007FF721DE8D80] [owr: 000001F7A74CBC00]
[000001F7B6E0B800] InterfaceProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityAdded:K2Node_DynamicCast_AsBPI_Generic_Character_Controller [o: 18] [n: 636D6E] [c: 00007FF721DE8ED0] [owr: 000001F7A74CBC00] [ic: 000001F4DC019190]
[000001F7B6E0B780] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityAdded:K2Node_DynamicCast_bSuccess [o: 28] [n: 59AAC7] [c: 00007FF721DE8D80] [owr: 000001F7A74CBC00]
[000001F7B6E0B700] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityAdded:CallFunc_GetGameplayAbilityTracker_ReturnValue [o: 30] [n: 6373C6] [c: 00007FF721DE9420] [owr: 000001F7A74CBC00] [pc: 000001F4DC019520]
[000001F7B6E0B680] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityAdded:CallFunc_IsValid_ReturnValue_1 [o: 38] [n: 59AA4C] [c: 00007FF721DE8D80] [owr: 000001F7A74CBC00]
[000001F7B6E0B600] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityAdded:CallFunc_GetController_ReturnValue [o: 40] [n: 61FF56] [c: 00007FF721DE9420] [owr: 000001F7A74CBC00] [pc: 00007FF46EF37F38]
[000001F7B6E0D680] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityAdded:CallFunc_IsPlayerActor_ReturnValue [o: 48] [n: 62054D] [c: 00007FF721DE8D80] [owr: 000001F7A74CBC00]
[000001F7B6E0D600] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:TrackUpgradeAbilityAdded:CallFunc_GetSpartaCharacterFromActorInfo_ReturnValue [o: 50] [n: 5FF5C1] [c: 00007FF721DE9420] [owr: 000001F7A74CBC00] [pc: 00007FF46F03C538]
[000001F7A74CC200] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:OnTick_D7F6C58E421495C479FB68AA5FCC4478 [n: 751675] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F78B547A90] FloatProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:OnTick_D7F6C58E421495C479FB68AA5FCC4478:DeltaTime [o: 0] [n: 5249B2] [c: 00007FF721DE9380] [owr: 000001F7A74CC200]
[000001F7A74CC400] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:K2_OnEndAbility [n: E901] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F7B6E0C380] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:K2_OnEndAbility:bWasCancelled [o: 0] [n: 559001] [c: 00007FF721DE8D80] [owr: 000001F7A74CC400]
[000001F7A74C8500] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:K2_CommitExecute [n: E8F8] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F7A74CBF00] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:SetTick [n: 659ADE] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F7B6E0BF00] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:SetTick:Enabled [o: 0] [n: 60DDC] [c: 00007FF721DE8D80] [owr: 000001F7A74CBF00]
[000001F7A74CC300] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:OnTick [n: 72B0] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F78B547B00] DoubleProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:OnTick:DeltaTime [o: 0] [n: 5249B2] [c: 00007FF721DE93D0] [owr: 000001F7A74CC300]
[000001F7A74CBE00] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:SpartaUpgradeAbilityCommitted [n: 751410] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F7B6E0BE80] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:SpartaUpgradeAbilityCommitted:CommittedFromEvent [o: 0] [n: 5FF5F1] [c: 00007FF721DE8D80] [owr: 000001F7A74CBE00]
[000001F7B6E0BE00] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:SpartaUpgradeAbilityCommitted:EventData [o: 8] [n: 524EDF] [c: 00007FF721DF9890] [owr: 000001F7A74CBE00] [ss: 00007FF46F38B678]
[000001F7A74CC600] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:K2_OnAbilityAdded [n: 14862] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F7A74C8600] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:K2_ActivateAbilityFromEvent [n: E8DD] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F7B6E0C400] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:K2_ActivateAbilityFromEvent:EventData [o: 0] [n: 524EDF] [c: 00007FF721DF9890] [owr: 000001F7A74C8600] [ss: 00007FF46F38B678]
[000001F7A74CC500] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:K2_OnAbilityRemoved [n: 1486C] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F7A74C8700] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:K2_ActivateAbility [n: E8D3] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F7A74C9B00] Function /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:ExecuteUbergraph_GA_UpgradeBase [n: 751590] [c: 00007FF46EE55538] [or: 000001F7A75A3FA0] [f: 00007FF7183D9710]
[000001F78B5470F0] IntProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:ExecuteUbergraph_GA_UpgradeBase:EntryPoint [o: 0] [n: 4E5223] [c: 00007FF721DE91F0] [owr: 000001F7A74C9B00]
[000001F78B546600] FloatProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:ExecuteUbergraph_GA_UpgradeBase:K2Node_CustomEvent_DeltaTime_1 [o: 4] [n: 6526DC] [c: 00007FF721DE9380] [owr: 000001F7A74C9B00]
[000001F7B6E07900] DelegateProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:ExecuteUbergraph_GA_UpgradeBase:K2Node_CreateDelegate_OutputDelegate [o: 8] [n: 59AA8D] [c: 00007FF721DE8E70] [owr: 000001F7A74C9B00] [df: 00007FF46F6B7198]
[000001F78B546590] FloatProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:ExecuteUbergraph_GA_UpgradeBase:Temp_real_Variable [o: 18] [n: 59B43D] [c: 00007FF721DE9380] [owr: 000001F7A74C9B00]
[000001F7B6E07880] ObjectProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:ExecuteUbergraph_GA_UpgradeBase:CallFunc_TickUntilCancelled_ReturnValue [o: 20] [n: 65256B] [c: 00007FF721DE9420] [owr: 000001F7A74C9B00] [pc: 00007FF46F003738]
[000001F7B6E07800] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:ExecuteUbergraph_GA_UpgradeBase:CallFunc_IsValid_ReturnValue [o: 28] [n: 59AA4C] [c: 00007FF721DE8D80] [owr: 000001F7A74C9B00]
[000001F7B6E07780] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:ExecuteUbergraph_GA_UpgradeBase:K2Node_Event_bWasCancelled [o: 29] [n: 61E2DB] [c: 00007FF721DE8D80] [owr: 000001F7A74C9B00]
[000001F7B6E07700] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:ExecuteUbergraph_GA_UpgradeBase:CallFunc_K2_CommitAbility_ReturnValue [o: 2A] [n: 669162] [c: 00007FF721DE8D80] [owr: 000001F7A74C9B00]
[000001F7B6E07680] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:ExecuteUbergraph_GA_UpgradeBase:K2Node_CustomEvent_Enabled [o: 2B] [n: 669251] [c: 00007FF721DE8D80] [owr: 000001F7A74C9B00]
[000001F78B546520] DoubleProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:ExecuteUbergraph_GA_UpgradeBase:K2Node_CustomEvent_DeltaTime [o: 30] [n: 6526DC] [c: 00007FF721DE93D0] [owr: 000001F7A74C9B00]
[000001F7B6E07600] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:ExecuteUbergraph_GA_UpgradeBase:K2Node_CustomEvent_CommittedFromEvent [o: 38] [n: 66923D] [c: 00007FF721DE8D80] [owr: 000001F7A74C9B00]
[000001F7B6E07580] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:ExecuteUbergraph_GA_UpgradeBase:K2Node_CustomEvent_EventData [o: 40] [n: 59AAA0] [c: 00007FF721DF9890] [owr: 000001F7A74C9B00] [ss: 00007FF46F38B678]
[000001F7B6E07500] StructProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:ExecuteUbergraph_GA_UpgradeBase:K2Node_Event_EventData [o: F0] [n: 5FF625] [c: 00007FF721DF9890] [owr: 000001F7A74C9B00] [ss: 00007FF46F38B678]
[000001F7B6E07480] BoolProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:ExecuteUbergraph_GA_UpgradeBase:CallFunc_K2_CommitAbility_ReturnValue_1 [o: 1A0] [n: 669162] [c: 00007FF721DE8D80] [owr: 000001F7A74C9B00]
[000001F78B548190] DoubleProperty /Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C:ExecuteUbergraph_GA_UpgradeBase:CallFunc_OnTick_DeltaTime_ImplicitCast [o: 1A8] [n: 66919E] [c: 00007FF721DE93D0] [owr: 000001F7A74C9B00]
```

---

## 12. Consumables: PP items and heal charges

Two independent mechanisms:

* **Heal charges** (flasks) -- abilities in `/Game/Sparta/Core/Player/Ability/Charges/`, documented in
  part 1 section 7.8, driven by `UPlayerAttributeSet:HealCurrentCharges/MaxCharges/AmountPerCharge`.
* **PP items** ("permanent progression" consumables, `/Game/Sparta/Items/Pickups/PermProgression/GA/`) --
  20 ability classes: one handler (`GA_PPItemBase`, `GA_PPItem_Active`) plus one per item family
  (Grisha, Mango, Moonshine, Weltcap, Mitigation_*, Transform_*). Resource state is
  `UPlayerAttributeSet:PPItemResource / PPItemMaxResource / PPItemCost` (part 1 section 2.3).

| Class | Parent | Own props |
|---|---|---|
| `/Game/Sparta/Items/Pickups/PermProgression/BP/BP_Behavior_EquipPPItem.BP_Behavior_EquipPPItem_C` | `/Game/Sparta/Items/Pickups/Core/BP_Executable_UseItem.BP_Executable_UseItem_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/BP/BP_PP_PickupBehavior_Mango.BP_PP_PickupBehavior_Mango_C` | `/Game/Sparta/Core/World/Levels/Behavior/BP_PickupBehavior_Base.BP_PickupBehavior_Base_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/BP/BP_PP_Pickup_Ash_Martyrs.BP_PP_Pickup_Ash_Martyrs_C` | `/Game/Sparta/Items/Pickups/BP_CraftingItemPickup.BP_CraftingItemPickup_C` | yes (count 6) |
| `/Game/Sparta/Items/Pickups/PermProgression/BP/BP_PP_Pickup_Bloodseed.BP_PP_Pickup_Bloodseed_C` | `/Game/Sparta/Items/Pickups/BP_CraftingItemPickup.BP_CraftingItemPickup_C` | yes (count 7) |
| `/Game/Sparta/Items/Pickups/PermProgression/BP/BP_PP_Pickup_Effigy_Burned.BP_PP_Pickup_Effigy_Burned_C` | `/Game/Sparta/Items/Pickups/BP_CraftingItemPickup.BP_CraftingItemPickup_C` | yes (count 7) |
| `/Game/Sparta/Items/Pickups/PermProgression/BP/BP_PP_Pickup_Gear_Strange.BP_PP_Pickup_Gear_Strange_C` | `/Game/Sparta/Items/Pickups/BP_CraftingItemPickup.BP_CraftingItemPickup_C` | yes (count 8) |
| `/Game/Sparta/Items/Pickups/PermProgression/BP/BP_PP_Pickup_Moonshine_Inferior.BP_PP_Pickup_Moonshine_Inferior_C` | `/Game/Sparta/Items/Pickups/BP_CraftingItemPickup.BP_CraftingItemPickup_C` | yes (count 8) |
| `/Game/Sparta/Items/Pickups/PermProgression/BP/BP_PP_Pickup_Mushroom_Tarspore.BP_PP_Pickup_Mushroom_Tarspore_C` | `/Game/Sparta/Items/Pickups/BP_CraftingItemPickup.BP_CraftingItemPickup_C` | yes (count 7) |
| `/Game/Sparta/Items/Pickups/PermProgression/BP/BP_PP_Pickup_Mushroom_Weltcap.BP_PP_Pickup_Mushroom_Weltcap_C` | `/Game/Sparta/Items/Pickups/BP_CraftingItemPickup.BP_CraftingItemPickup_C` | yes (count 8) |
| `/Game/Sparta/Items/Pickups/PermProgression/BP/BP_PP_Pickup_Mushroom_Weltcap_Group.BP_PP_Pickup_Mushroom_Weltcap_Group_C` | `/Game/Sparta/Items/Pickups/BP_CraftingItemPickup.BP_CraftingItemPickup_C` | yes (count 16) |
| `/Game/Sparta/Items/Pickups/PermProgression/BP/BP_PP_Pickup_Trollweed.BP_PP_Pickup_Trollweed_C` | `/Game/Sparta/Items/Pickups/BP_CraftingItemPickup.BP_CraftingItemPickup_C` | yes (count 7) |
| `/Game/Sparta/Items/Pickups/PermProgression/BP/BP_PP_Pickup_UniqueScripture.BP_PP_Pickup_UniqueScripture_C` | `/Game/Sparta/Items/Pickups/BP_CraftingItemPickup.BP_CraftingItemPickup_C` | yes (count 8) |
| `/Game/Sparta/Items/Pickups/PermProgression/BP/BP_Pickup_Glimpse.BP_Pickup_Glimpse_C` | `/Game/Sparta/Items/Pickups/BP_CraftingItemPickup.BP_CraftingItemPickup_C` | yes (count 9) |
| `/Game/Sparta/Items/Pickups/PermProgression/CCC/CCC_PPIO_Bloodseed.CCC_PPIO_Bloodseed_C` | `/Game/Sparta/Items/Pickups/PermProgression/CCC/CCC_PPItemBaseOverflow.CCC_PPItemBaseOverflow_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/CCC/CCC_PPIO_BurnedEffigy.CCC_PPIO_BurnedEffigy_C` | `/Game/Sparta/Items/Pickups/PermProgression/CCC/CCC_PPItemBaseOverflow.CCC_PPItemBaseOverflow_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/CCC/CCC_PPIO_MartyrAsh.CCC_PPIO_MartyrAsh_C` | `/Game/Sparta/Items/Pickups/PermProgression/CCC/CCC_PPItemBaseOverflow.CCC_PPItemBaseOverflow_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/CCC/CCC_PPIO_Moonshine.CCC_PPIO_Moonshine_C` | `/Game/Sparta/Items/Pickups/PermProgression/CCC/CCC_PPItemBaseOverflow.CCC_PPItemBaseOverflow_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/CCC/CCC_PPIO_ParadoxicalScripture.CCC_PPIO_ParadoxicalScripture_C` | `/Game/Sparta/Items/Pickups/PermProgression/CCC/CCC_PPItemBaseOverflow.CCC_PPItemBaseOverflow_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/CCC/CCC_PPIO_StrangeGear.CCC_PPIO_StrangeGear_C` | `/Game/Sparta/Items/Pickups/PermProgression/CCC/CCC_PPItemBaseOverflow.CCC_PPItemBaseOverflow_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/CCC/CCC_PPIO_Tarspore.CCC_PPIO_Tarspore_C` | `/Game/Sparta/Items/Pickups/PermProgression/CCC/CCC_PPItemBaseOverflow.CCC_PPItemBaseOverflow_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/CCC/CCC_PPIO_UniqueScripture.CCC_PPIO_UniqueScripture_C` | `/Game/Sparta/Items/Pickups/PermProgression/CCC/CCC_PPItemBaseOverflow.CCC_PPItemBaseOverflow_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/CCC/CCC_PPIO_Weltcap.CCC_PPIO_Weltcap_C` | `/Game/Sparta/Items/Pickups/PermProgression/CCC/CCC_PPItemBaseOverflow.CCC_PPItemBaseOverflow_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/CCC/CCC_PPItemBaseOverflow.CCC_PPItemBaseOverflow_C` | `/Script/Sparta.SpartaModMagnitudeCalculation` | yes (count 5) |
| `/Game/Sparta/Items/Pickups/PermProgression/CCC/CCC_Sheep_Passive.CCC_Sheep_Passive_C` | `/Script/Sparta.SpartaModMagnitudeCalculation` | yes (count 3) |
| `/Game/Sparta/Items/Pickups/PermProgression/CCC/CCC_Weltcap_Active.CCC_Weltcap_Active_C` | `/Script/Sparta.SpartaModMagnitudeCalculation` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItemBase.GA_PPItemBase_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 2) |
| `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Active.GA_PPItem_Active_C` | `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItemBase.GA_PPItemBase_C` | yes (count 7) |
| `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Grisha_Active.GA_PPItem_Grisha_Active_C` | `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Active.GA_PPItem_Active_C` | yes (count 3) |
| `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Grisha_Passive.GA_PPItem_Grisha_Passive_C` | `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItemBase.GA_PPItemBase_C` | yes (count 3) |
| `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Handler_MartyrAsh.GA_PPItem_Handler_MartyrAsh_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Mango_Active.GA_PPItem_Mango_Active_C` | `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Active.GA_PPItem_Active_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Mango_Passive.GA_PPItem_Mango_Passive_C` | `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItemBase.GA_PPItemBase_C` | yes (count 3) |
| `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Mitigation_Active.GA_PPItem_Mitigation_Active_C` | `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Active.GA_PPItem_Active_C` | yes (count 1) |
| `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Mitigation_Bloodseed.GA_PPItem_Mitigation_Bloodseed_C` | `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Mitigation_Active.GA_PPItem_Mitigation_Active_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Mitigation_BurnedEffigy.GA_PPItem_Mitigation_BurnedEffigy_C` | `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Mitigation_Active.GA_PPItem_Mitigation_Active_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Mitigation_MartyrAsh.GA_PPItem_Mitigation_MartyrAsh_C` | `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Mitigation_Active.GA_PPItem_Mitigation_Active_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Mitigation_ParadoxicalScripture.GA_PPItem_Mitigation_ParadoxicalScripture_C` | `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Mitigation_Active.GA_PPItem_Mitigation_Active_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Mitigation_StrangeGear.GA_PPItem_Mitigation_StrangeGear_C` | `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Mitigation_Active.GA_PPItem_Mitigation_Active_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Mitigation_Tarspore.GA_PPItem_Mitigation_Tarspore_C` | `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Mitigation_Active.GA_PPItem_Mitigation_Active_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Mitigation_UniqueScripture.GA_PPItem_Mitigation_UniqueScripture_C` | `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Mitigation_Active.GA_PPItem_Mitigation_Active_C` | yes (count 1) |
| `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Moonshine_Active.GA_PPItem_Moonshine_Active_C` | `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Active.GA_PPItem_Active_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Transform_Bush.GA_PPItem_Transform_Bush_C` | `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Active.GA_PPItem_Active_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Transform_Sheep.GA_PPItem_Transform_Sheep_C` | `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Active.GA_PPItem_Active_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Weltcap_Active.GA_PPItem_Weltcap_Active_C` | `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Active.GA_PPItem_Active_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GA/GA_SummonedGrisha_Handler.GA_SummonedGrisha_Handler_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 4) |
| `/Game/Sparta/Items/Pickups/PermProgression/GA/GE_SummonedGrisha_Damage.GE_SummonedGrisha_Damage_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GE/GE_ConsumePPItem.GE_ConsumePPItem_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GE/GE_ModifyPPItemUses.GE_ModifyPPItemUses_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GE/GE_PP_Bloodseed_Active.GE_PP_Bloodseed_Active_C` | `/Game/Sparta/Core/Effects/StatusEffects/GE_SpartaStatusEffectBase.GE_SpartaStatusEffectBase_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GE/GE_PP_Bloodseed_Passive.GE_PP_Bloodseed_Passive_C` | `/Game/Sparta/Core/Effects/StatusEffects/GE_SpartaStatusEffectBase.GE_SpartaStatusEffectBase_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GE/GE_PP_BurnedEffigy_Active.GE_PP_BurnedEffigy_Active_C` | `/Game/Sparta/Core/Effects/StatusEffects/GE_SpartaStatusEffectBase.GE_SpartaStatusEffectBase_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GE/GE_PP_BurnedEffigy_Passive.GE_PP_BurnedEffigy_Passive_C` | `/Game/Sparta/Core/Effects/StatusEffects/GE_SpartaStatusEffectBase.GE_SpartaStatusEffectBase_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GE/GE_PP_Grisha_Passive.GE_PP_Grisha_Passive_C` | `/Game/Sparta/Core/Effects/StatusEffects/GE_SpartaStatusEffectBase.GE_SpartaStatusEffectBase_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GE/GE_PP_Mango_Passive.GE_PP_Mango_Passive_C` | `/Game/Sparta/Core/Effects/StatusEffects/GE_SpartaStatusEffectBase.GE_SpartaStatusEffectBase_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GE/GE_PP_MartyrAsh_Active.GE_PP_MartyrAsh_Active_C` | `/Game/Sparta/Core/Effects/StatusEffects/GE_SpartaStatusEffectBase.GE_SpartaStatusEffectBase_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GE/GE_PP_MartyrAsh_Passive.GE_PP_MartyrAsh_Passive_C` | `/Game/Sparta/Core/Effects/StatusEffects/GE_SpartaStatusEffectBase.GE_SpartaStatusEffectBase_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GE/GE_PP_Moonshine_Passive.GE_PP_Moonshine_Passive_C` | `/Game/Sparta/Core/Effects/StatusEffects/GE_SpartaStatusEffectBase.GE_SpartaStatusEffectBase_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GE/GE_PP_ParadoxicalScripture_Active.GE_PP_ParadoxicalScripture_Active_C` | `/Game/Sparta/Core/Effects/StatusEffects/GE_SpartaStatusEffectBase.GE_SpartaStatusEffectBase_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GE/GE_PP_ParadoxicalScripture_Passive.GE_PP_ParadoxicalScripture_Passive_C` | `/Game/Sparta/Core/Effects/StatusEffects/GE_SpartaStatusEffectBase.GE_SpartaStatusEffectBase_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GE/GE_PP_Sheep_Passive_AI.GE_PP_Sheep_Passive_AI_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GE/GE_PP_Sheep_Passive_Player.GE_PP_Sheep_Passive_Player_C` | `/Game/Sparta/Core/Effects/StatusEffects/GE_SpartaStatusEffectBase.GE_SpartaStatusEffectBase_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GE/GE_PP_StrangeGear_Active.GE_PP_StrangeGear_Active_C` | `/Game/Sparta/Core/Effects/StatusEffects/GE_SpartaStatusEffectBase.GE_SpartaStatusEffectBase_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GE/GE_PP_StrangeGear_Passive.GE_PP_StrangeGear_Passive_C` | `/Game/Sparta/Core/Effects/StatusEffects/GE_SpartaStatusEffectBase.GE_SpartaStatusEffectBase_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GE/GE_PP_Tarspore_Active.GE_PP_Tarspore_Active_C` | `/Game/Sparta/Core/Effects/StatusEffects/GE_SpartaStatusEffectBase.GE_SpartaStatusEffectBase_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GE/GE_PP_Tarspore_Passive.GE_PP_Tarspore_Passive_C` | `/Game/Sparta/Core/Effects/StatusEffects/GE_SpartaStatusEffectBase.GE_SpartaStatusEffectBase_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GE/GE_PP_Trollweed_Passive.GE_PP_Trollweed_Passive_C` | `/Game/Sparta/Core/Effects/StatusEffects/GE_SpartaStatusEffectBase.GE_SpartaStatusEffectBase_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GE/GE_PP_UniqueScripture_Active.GE_PP_UniqueScripture_Active_C` | `/Game/Sparta/Core/Effects/StatusEffects/GE_SpartaStatusEffectBase.GE_SpartaStatusEffectBase_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GE/GE_PP_UniqueScripture_Passive.GE_PP_UniqueScripture_Passive_C` | `/Game/Sparta/Core/Effects/StatusEffects/GE_SpartaStatusEffectBase.GE_SpartaStatusEffectBase_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GE/GE_PP_UniqueScripture_Stun.GE_PP_UniqueScripture_Stun_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GE/GE_PP_Weltcap_Active.GE_PP_Weltcap_Active_C` | `/Game/Sparta/Core/Effects/StatusEffects/GE_SpartaStatusEffectBase.GE_SpartaStatusEffectBase_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GE/GE_PP_Weltcap_Passive.GE_PP_Weltcap_Passive_C` | `/Game/Sparta/Core/Effects/StatusEffects/GE_SpartaStatusEffectBase.GE_SpartaStatusEffectBase_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GE/GE_RechargePPItem.GE_RechargePPItem_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/GE/GE_Sheep_Invisible.GE_Sheep_Invisible_C` | `/Script/Sparta.SpartaGameplayEffect` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Base.ID_PP_Base_C` | `/Game/Sparta/Items/Pickups/ID_PickupBase.ID_PickupBase_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Bloodseed.ID_PP_Bloodseed_C` | `/Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Base.ID_PP_Base_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Effigy_Burned.ID_PP_Effigy_Burned_C` | `/Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Base.ID_PP_Base_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Gear_Strange.ID_PP_Gear_Strange_C` | `/Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Base.ID_PP_Base_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Grisha.ID_PP_Grisha_C` | `/Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Base.ID_PP_Base_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_HealingAmount.ID_PP_HealingAmount_C` | `/Game/Sparta/Items/Pickups/ID_PickupBase.ID_PickupBase_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_HealingCharge.ID_PP_HealingCharge_C` | `/Game/Sparta/Items/Pickups/ID_PickupBase.ID_PickupBase_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Mango.ID_PP_Mango_C` | `/Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Base.ID_PP_Base_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_MartyrAsh.ID_PP_MartyrAsh_C` | `/Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Base.ID_PP_Base_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Moonshine_Inferior.ID_PP_Moonshine_Inferior_C` | `/Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Base.ID_PP_Base_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Mushroom_Weltcap.ID_PP_Mushroom_Weltcap_C` | `/Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Base.ID_PP_Base_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_ParadoxicalScripture.ID_PP_ParadoxicalScripture_C` | `/Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Base.ID_PP_Base_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Sheep.ID_PP_Sheep_C` | `/Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Base.ID_PP_Base_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Tarspore.ID_PP_Tarspore_C` | `/Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Base.ID_PP_Base_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Trollweed.ID_PP_Trollweed_C` | `/Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Base.ID_PP_Base_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_UniqueScripture.ID_PP_UniqueScripture_C` | `/Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Base.ID_PP_Base_C` | none |
| `/Game/Sparta/Items/Pickups/PermProgression/IF/IF_PPItem.IF_PPItem_C` | `/Script/Sparta.PPItemFragment` | yes (count 11) |

Evidence - PP item ability class rows:
```
[000001F4DC0186E0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItemBase.GA_PPItemBase_C [n: 2970FE] [c: 00007FF46EEC3D38] [or: 000001F4C0A35790] [sps: 000001F4DC018E00]
[000001F614CB46C0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Active.GA_PPItem_Active_C [n: 3D5464] [c: 00007FF46EEC3D38] [or: 000001F4C5867770] [sps: 000001F4DC0186E0]
[000001F614CB3880] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Weltcap_Active.GA_PPItem_Weltcap_Active_C [n: 3817D5] [c: 00007FF46EEC3D38] [or: 000001F4C58677C0] [sps: 000001F614CB46C0]
[000001F74659E380] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Moonshine_Active.GA_PPItem_Moonshine_Active_C [n: 2A8752] [c: 00007FF46EEC3D38] [or: 000001F746698E40] [sps: 000001F614CB46C0]
[000001F76BAE5170] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Mitigation_Active.GA_PPItem_Mitigation_Active_C [n: 2151E9] [c: 00007FF46EEC3D38] [or: 000001F4DD94C1D0] [sps: 000001F614CB46C0]
[000001F76BAE4A50] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Mitigation_StrangeGear.GA_PPItem_Mitigation_StrangeGear_C [n: 237561] [c: 00007FF46EEC3D38] [or: 000001F4DD94B2D0] [sps: 000001F76BAE5170]
[000001F7DCB70310] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Mitigation_Tarspore.GA_PPItem_Mitigation_Tarspore_C [n: 1EE3CF] [c: 00007FF46EEC3D38] [or: 000001F7F6DE80D0] [sps: 000001F76BAE5170]
[000001F7F1C33FA0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Handler_MartyrAsh.GA_PPItem_Handler_MartyrAsh_C [n: 1856C6] [c: 00007FF46EEC3D38] [or: 000001F7F719E660] [sps: 000001F4DC018E00]
[000001F7F1C326B0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Mitigation_MartyrAsh.GA_PPItem_Mitigation_MartyrAsh_C [n: 1A8D00] [c: 00007FF46EEC3D38] [or: 000001F7F719E4D0] [sps: 000001F76BAE5170]
[000001F7FC0DBC50] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/GA/GA_SummonedGrisha_Handler.GA_SummonedGrisha_Handler_C [n: E1FDA] [c: 00007FF46EEC3D38] [or: 000001F80A6E32B0] [sps: 000001F4DC018E00]
[000001F7FD95B8C0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Grisha_Passive.GA_PPItem_Grisha_Passive_C [n: 1F27E3] [c: 00007FF46EEC3D38] [or: 000001F80A6E3300] [sps: 000001F4DC0186E0]
[000001F7FD959FD0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Grisha_Active.GA_PPItem_Grisha_Active_C [n: 4ADE82] [c: 00007FF46EEC3D38] [or: 000001F80A6E3120] [sps: 000001F614CB46C0]
[000001F8104286E0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Mitigation_ParadoxicalScripture.GA_PPItem_Mitigation_ParadoxicalScripture_C [n: 1C6D8D] [c: 00007FF46EEC3D38] [or: 000001F81ABB0470] [sps: 000001F76BAE5170]
[000001F819DA3880] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Transform_Sheep.GA_PPItem_Transform_Sheep_C [n: 2746FE] [c: 00007FF46EEC3D38] [or: 000001F81077AA60] [sps: 000001F614CB46C0]
[000001F83C3E26B0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Mitigation_BurnedEffigy.GA_PPItem_Mitigation_BurnedEffigy_C [n: D4C94] [c: 00007FF46EEC3D38] [or: 000001F8394EF600] [sps: 000001F76BAE5170]
[000001F8437B8350] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Mitigation_Bloodseed.GA_PPItem_Mitigation_Bloodseed_C [n: 1CF61D] [c: 00007FF46EEC3D38] [or: 000001F843768080] [sps: 000001F76BAE5170]
[000001F8C883D1B0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Mitigation_UniqueScripture.GA_PPItem_Mitigation_UniqueScripture_C [n: 378C7A] [c: 00007FF46EEC3D38] [or: 000001F8C7F0BC80] [sps: 000001F76BAE5170]
[000001F97582E710] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Mango_Active.GA_PPItem_Mango_Active_C [n: 43CC7B] [c: 00007FF46EEC3D38] [or: 000001F9755BF100] [sps: 000001F614CB46C0]
[000001F97582DC60] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Mango_Passive.GA_PPItem_Mango_Passive_C [n: 3F9BCF] [c: 00007FF46EEC3D38] [or: 000001F9755BF010] [sps: 000001F4DC0186E0]
[000001F9758FBFE0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/GA/GA_PPItem_Transform_Bush.GA_PPItem_Transform_Bush_C [n: 3EC426] [c: 00007FF46EEC3D38] [or: 000001F9758D2220] [sps: 000001F614CB46C0]
```

The **use-item input** is `GA_Action_UseItem_C` (`/Game/Sparta/Core/Player/Ability/Input/`, part 1
section 8) and the animation for using an item is a plain montage ability; the montage assets exist as
`/Game/Sparta/Characters/Shells/_Shared/Animation/Actions/UseItems/A_Shared_Action_UseItem_Temp_Montage.A_Shared_Action_UseItem_Temp_Montage`
and `..._UseScripture_Temp_Montage...` (name match only -- GUESS that these are the assigned ones).

---

## 14. Traversal, fast travel, transformation, rest

### 14.1 `/Game/Sparta/Core/Player/Ability/Traversal/` plus the outer traversal abilities

| Class | Parent | Own props (offset) |
|---|---|---|
| `.../Traversal/GA_Traversal_EnterBeacon.GA_Traversal_EnterBeacon_C` | `GA_PlayMontageBase_C` | none |
| `.../Traversal/GA_Traversal_FastTravel.GA_Traversal_FastTravel_C` | `GA_PlayMontageBase_C` | `CameraStateClass: ClassProperty`, `MyCameraState` |
| `.../Traversal/GA_Traversal_FastTravel_HUB.GA_Traversal_FastTravel_HUB_C` | `GA_Traversal_FastTravel_C` | none |
| `.../Traversal/GA_Traversal_FastTravel_HUB_LoseGloom.GA_Traversal_FastTravel_HUB_LoseGloom_C` | `GA_Traversal_FastTravel_HUB_C` | none |
| `.../Traversal/GA_Traversal_SqueezeThrough.GA_Traversal_SqueezeThrough_C` | `GA_SpartaAbility_C` | `SqueezeMontage: ObjectProperty of Engine.AnimMontage`, `bDrawEventSent: bool` |
| `.../Traversal/GA_Traversal_SqueezeThrough_NoWeaponStow.GA_Traversal_SqueezeThrough_NoWeaponStow_C` | `GA_SpartaAbility_C` | `Montage: ObjectProperty of Engine.AnimMontage` |
| `.../Traversal/GA_Traversal_Vault.GA_Traversal_Vault_C` | `GA_SpartaAbility_C` | `Montage: ObjectProperty of Engine.AnimMontage` |
| `.../Ability/GA_Traversal_Vault_Old.GA_Traversal_Vault_Old_C` | `GA_SpartaAbility_C` | `Warp` |
| `.../Ability/GA_Traversal_Teleport.GA_Traversal_Teleport_C` | `GA_SpartaAbility_C` | `Warp` |
| `.../Ability/GA_Traversal_BoneGate_Far.GA_Traversal_BoneGate_Far_C` | `GA_SpartaAbility_C` | 11 props: `ShellCharacter`, `WarpTargets`, `PreTeleportAnimation`, `PostTeleportAnimation`, `InstigatorObject`, `WarpOffset`, `InstigatorActor`, `SpawnedStreamingSource`, `MyCameraState`, `WeaponSlots`, `bTraversalEnded` |
| `.../Ability/GA_Traversal_ShellThrow.GA_Traversal_ShellThrow_C` | `GA_SpartaAbility_C` | 7 props: `ShellCharacter`, `WarpTargets`, `TimedBlockDuration`, `PlayerAnimation`, `ShellAnimation`, `InstigatorObject`, `WarpOffset` |
| `.../Ability/GA_Traversal_ShellThrow_High.GA_Traversal_ShellThrow_High_C` | `GA_Traversal_ShellThrow_C` | none |
| `.../Ability/GA_Traversal_ShellThrow_Long.GA_Traversal_ShellThrow_Long_C` | `GA_Traversal_ShellThrow_C` | none |
| `.../Ability/GA_TeleportPlayerWithStreamingSupport.GA_TeleportPlayerWithStreamingSupport_C` | `GA_SpartaAbility_C` | 6 props: `SpawnedStreamingSource`, `EventData`, `CachedFadeOutDuration`, `MySpartaCharacter`, `Invulnerability`, `FallDamageImmunity` |

`GA_Traversal_ShellThrow_C` is a **two-actor** ability like the riposte: it holds `ShellCharacter`
(the thrown shell) *and* the player's own animation (`PlayerAnimation`) plus `WarpTargets` and
`TimedBlockDuration` -- replicate both montages + the warp targets or the throw desynchronises.

### 14.2 `/Game/Sparta/Core/Player/Ability/Transformation/` - 7 ability classes

| Class | Parent | Own props |
|---|---|---|
| `.../Transformation/GA_TransformationHandlerBase.GA_TransformationHandlerBase_C` | `GA_SpartaAbility_C` | 35 (below) |
| `.../Transformation/GA_Transformation_Bush.GA_Transformation_Bush_C` | `GA_TransformationHandlerBase_C` | none |
| `.../Transformation/GA_Transformation_Mango.GA_Transformation_Mango_C` | `GA_Transformation_Bush_C` | none |
| `.../Transformation/GA_Transformation_Sheep.GA_Transformation_Sheep_C` | `GA_TransformationHandlerBase_C` | 7 |
| `.../Transformation/GA_Transformation_Sheep_PPItem.GA_Transformation_Sheep_PPItem_C` | `GA_Transformation_Sheep_C` | `InvisibilityGE`, `bWasSevered` |
| `.../Transformation/GA_Transformation_Sheep_Shepherd.GA_Transformation_Sheep_Shepherd_C` | `GA_Transformation_Sheep_C` | none |
| `.../Transformation/GA_Transformation_TarredCorpse.GA_Transformation_TarredCorpse_C` | `GA_TransformationHandlerBase_C` | 8 |

`GA_TransformationHandlerBase_C` (35 properties -- the whole "become a prop/animal" state):
`TransformationID`, `TransformationStateGE`, `ActiveCameraState`, `CameraState`, `WeaponsToHide`,
`SkeletalMesh`, `RotationRate`, `MovementSpeed`, `SeveredWaitTask`, `HitReactionWaitTask`,
`StaticMesh`, `LocomotionBlendSpace`, `OriginalMesh`, `OriginalCharacterData`, `WeaponsToStow`,
`StateHandle`, `LoadedSkeletalMesh`, `AudioDataOverride`, `LoadedStaticMesh`, `AudioLoopId`,
`AudioStartEvent`, `AudioLoopEvent`, `AudioEndEvent`, `AudioLoopLocation`, `OriginalAudioData`,
`AudioStartLocation`, `TransformCue`, `DetransformCue`, `AudioEndLocation`, `TransformMontage`,
`MontagePlayRate`, `BlockedAbilities`, `StaticMeshComponent`, `StaticMeshActor`, `ListenToSingleStop`.
`GA_Transformation_Sheep_C` adds ability handles: `LightAttackHandle`, `HeavyAttackHandle`,
`JumpAttackHandle`, `DashAbilityHandle`, `SprintAbilityHandle`, `CalmAbilityHandle`, `CustomWeapons`.

`TransformMontage` + `TransformationID` + `MovementSpeed` are the fields to mirror; the transformation
grants/removes ability handles, i.e. **the ability grant set changes at runtime** (see
`CharacterPersistantAbilitySetData` in part 1 section 1.2).

### 14.3 Rest / sit

There is no `GA_Rest`: "rest" is the pair `GA_Action_SitDown_C` (part 1 section 8) plus
`.../Interactions/GA_Player_SitDown_C`, `GA_Player_SitThenLoop_WithCancel_C`,
`GA_Player_SitThenLoop_Vista_C`, `GA_Player_StandFromSit_C`, `GA_Player_DrinkWhileSitting_C`,
`GA_Gragu_TavernSitLoop_C`, `GA_Gragu_PoisonedSitting_C` (all `GA_PlayMontageBase_C` children, all in
part 2 section 13). Fast travel is `GA_Traversal_FastTravel*` above.


Evidence - traversal and transformation class rows:
```
[000001F4C2859520] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/GA_Traversal_BoneGate_Far.GA_Traversal_BoneGate_Far_C [n: 4BD929] [c: 00007FF46EEC3D38] [or: 000001F4D1C1A8D0] [sps: 000001F4DC018E00]
[000001F4C28578A0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/GA_Traversal_ShellThrow.GA_Traversal_ShellThrow_C [n: 3D8282] [c: 00007FF46EEC3D38] [or: 000001F4CEED1000] [sps: 000001F4DC018E00]
[000001F4C285FC70] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/GA_Traversal_ShellThrow_High.GA_Traversal_ShellThrow_High_C [n: 47D174] [c: 00007FF46EEC3D38] [or: 000001F4CEED12D0] [sps: 000001F4C28578A0]
[000001F4C285EE30] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/GA_Traversal_ShellThrow_Long.GA_Traversal_ShellThrow_Long_C [n: 320C7B] [c: 00007FF46EEC3D38] [or: 000001F4CEED1320] [sps: 000001F4C28578A0]
[000001F617FAB8C0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/GA_TeleportPlayerWithStreamingSupport.GA_TeleportPlayerWithStreamingSupport_C [n: 1C116E] [c: 00007FF46EEC3D38] [or: 000001F4CEED0060] [sps: 000001F4DC018E00]
[000001F61927E710] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Traversal/GA_Traversal_EnterBeacon.GA_Traversal_EnterBeacon_C [n: 2D2896] [c: 00007FF46EEC3D38] [or: 000001F4D1C17A40] [sps: 000001F615242DD0]
[000001F619929C40] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Traversal/GA_Traversal_FastTravel.GA_Traversal_FastTravel_C [n: 17F2EF] [c: 00007FF46EEC3D38] [or: 000001F4D1C19980] [sps: 000001F615242DD0]
[000001F6199298B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Traversal/GA_Traversal_FastTravel_HUB.GA_Traversal_FastTravel_HUB_C [n: 4B92F1] [c: 00007FF46EEC3D38] [or: 000001F4D1C19890] [sps: 000001F619929C40]
[000001F614CCC700] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Traversal/GA_Traversal_FastTravel_HUB_LoseGloom.GA_Traversal_FastTravel_HUB_LoseGloom_C [n: 1DF040] [c: 00007FF46EEC3D38] [or: 000001F4D1C197A0] [sps: 000001F6199298B0]
[000001F621EF1150] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Transformation/GA_TransformationHandlerBase.GA_TransformationHandlerBase_C [n: 1D6580] [c: 00007FF46EEC3D38] [or: 000001F4D1C17D60] [sps: 000001F4DC018E00]
[000001F621EFEAA0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Transformation/GA_Transformation_TarredCorpse.GA_Transformation_TarredCorpse_C [n: 18CDC1] [c: 00007FF46EEC3D38] [or: 000001F4D1C16DC0] [sps: 000001F621EF1150]
[000001F80D7A8E00] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Transformation/GA_Transformation_Sheep.GA_Transformation_Sheep_C [n: 1F0D98] [c: 00007FF46EEC3D38] [or: 000001F80D67C630] [sps: 000001F621EF1150]
[000001F80D7A86E0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Transformation/GA_Transformation_Sheep_Shepherd.GA_Transformation_Sheep_Shepherd_C [n: B5079] [c: 00007FF46EEC3D38] [or: 000001F80D67C5E0] [sps: 000001F80D7A8E00]
[000001F819DA3FA0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Transformation/GA_Transformation_Sheep_PPItem.GA_Transformation_Sheep_PPItem_C [n: E04A0] [c: 00007FF46EEC3D38] [or: 000001F81077A920] [sps: 000001F80D7A8E00]
[000001F8CA0B7C30] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Traversal/GA_Traversal_Vault.GA_Traversal_Vault_C [n: F1A19] [c: 00007FF46EEC3D38] [or: 000001F634ED8C10] [sps: 000001F4DC018E00]
[000001F8CA5E26B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Traversal/GA_Traversal_SqueezeThrough.GA_Traversal_SqueezeThrough_C [n: 302AF8] [c: 00007FF46EEC3D38] [or: 000001F634EDAEC0] [sps: 000001F4DC018E00]
[000001F8CA5E2A40] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Traversal/GA_Traversal_SqueezeThrough_NoWeaponStow.GA_Traversal_SqueezeThrough_NoWeaponStow_C [n: 77760] [c: 00007FF46EEC3D38] [or: 000001F634ED9CA0] [sps: 000001F4DC018E00]
[000001F8CA5E7C30] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/GA_Traversal_Teleport.GA_Traversal_Teleport_C [n: 195AE0] [c: 00007FF46EEC3D38] [or: 000001F634EDBB40] [sps: 000001F4DC018E00]
[000001F8CA5E7FC0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/GA_Traversal_Vault_Old.GA_Traversal_Vault_Old_C [n: 353C02] [c: 00007FF46EEC3D38] [or: 000001F634EDBAA0] [sps: 000001F4DC018E00]
[000001F8D8321C00] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Transformation/GA_Transformation_Bush.GA_Transformation_Bush_C [n: B0605] [c: 00007FF46EEC3D38] [or: 000001F8D760E2F0] [sps: 000001F621EF1150]
[000001F8D8320DC0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Transformation/GA_Transformation_Mango.GA_Transformation_Mango_C [n: 2D2888] [c: 00007FF46EEC3D38] [or: 000001F8D760F600] [sps: 000001F8D8321C00]
```

Evidence - the two-actor traversal/transformation fields:
```
[000001F61520F080] ObjectProperty /Game/Sparta/Core/Player/Ability/GA_Traversal_ShellThrow.GA_Traversal_ShellThrow_C:ShellCharacter [o: 518] [n: 65206D] [c: 00007FF721DE9420] [owr: 000001F4C28578A0] [pc: 00007FF46F03C538]
[000001F615213180] FloatProperty /Game/Sparta/Core/Player/Ability/GA_Traversal_ShellThrow.GA_Traversal_ShellThrow_C:TimedBlockDuration [o: 530] [n: 6542BD] [c: 00007FF721DE9380] [owr: 000001F4C28578A0]
[000001F61520EF00] ObjectProperty /Game/Sparta/Core/Player/Ability/GA_Traversal_ShellThrow.GA_Traversal_ShellThrow_C:PlayerAnimation [o: 538] [n: 65428E] [c: 00007FF721DE9420] [owr: 000001F4C28578A0] [pc: 00007FF46EF2E338]
[000001F61520EE80] ObjectProperty /Game/Sparta/Core/Player/Ability/GA_Traversal_ShellThrow.GA_Traversal_ShellThrow_C:ShellAnimation [o: 540] [n: 6542AD] [c: 00007FF721DE9420] [owr: 000001F4C28578A0] [pc: 00007FF46EF2E338]
[000001F622320500] StructProperty /Game/Sparta/Core/Player/Ability/Transformation/GA_TransformationHandlerBase.GA_TransformationHandlerBase_C:TransformationID [o: 518] [n: 668CED] [c: 00007FF721DF9890] [owr: 000001F621EF1150] [ss: 00007FF46F3890A8]
[000001F621F5AB90] FloatProperty /Game/Sparta/Core/Player/Ability/Transformation/GA_TransformationHandlerBase.GA_TransformationHandlerBase_C:MovementSpeed [o: 584] [n: 668C44] [c: 00007FF721DE9380] [owr: 000001F621EF1150]
[000001F622321880] ObjectProperty /Game/Sparta/Core/Player/Ability/Transformation/GA_TransformationHandlerBase.GA_TransformationHandlerBase_C:TransformMontage [o: 668] [n: 668E02] [c: 00007FF721DE9420] [owr: 000001F621EF1150] [pc: 00007FF46EF2E338]
[000001F826501700] StructProperty /Game/Sparta/Core/Player/Ability/Transformation/GA_Transformation_Sheep.GA_Transformation_Sheep_C:LightAttackHandle [o: 6B8] [n: 65FF7B] [c: 00007FF721DF9890] [owr: 000001F80D7A8E00] [ss: 00007FF46F388980]
[000001F826501580] StructProperty /Game/Sparta/Core/Player/Ability/Transformation/GA_Transformation_Sheep.GA_Transformation_Sheep_C:DashAbilityHandle [o: 6C4] [n: 76C202] [c: 00007FF721DF9890] [owr: 000001F80D7A8E00] [ss: 00007FF46F388980]
[000001F8C343EF00] ObjectProperty /Game/Sparta/Core/Player/Ability/Traversal/GA_Traversal_SqueezeThrough.GA_Traversal_SqueezeThrough_C:SqueezeMontage [o: 518] [n: 846178] [c: 00007FF721DE9420] [owr: 000001F8CA5E26B0] [pc: 00007FF46EF2E338]
```

---

## 13. Interaction / memory abilities - `/Game/Sparta/Core/Player/Ability/Interactions/`

193 classes: 191 are children of `GA_PlayMontageBase_C`, 2 are children of `GA_SpartaAbility_C`
(verified with `GROUP BY parent`). They are the "press interact, play this montage" abilities: examine,
kneel, pray, take item, sit, lie down, punch a tree, bottle-kick, etc. A few are story-memory
re-enactments (`GA_Tiel_*`, `GA_Smert_*`, `GA_Sariel_*`, `GA_Eredrim_*`, `GA_Proxima_*`,
`GA_Player_*_Memory`).

All 193 declare **no own property** except where noted in the `Own props` column: the montage and the
play-rate come from `GA_PlayMontageBase_C` (`Montage` `0x5E0`, `MontageStartSection` `0x600`), and the
role of the ability is entirely its montage + the `Interact` input tag it is granted against.

Sync relevance is low for a co-op layer: these are local/cinematic. The exceptions worth mirroring are
the ones that move the *world* or another actor -- `GA_ShellRevive*` / `GA_ShellDowned*` (section 10.2),
`GA_Player_Take*` (pickups) and the `Memories/` ones that spawn NPCs.

| Class | Parent | Own props |
|---|---|---|
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_EredrimOnly_ExamineKneel_Loop.GA_EredrimOnly_ExamineKneel_Loop_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_EredrimOnly_ExamineKneel_Rise.GA_EredrimOnly_ExamineKneel_Rise_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_EredrimOnly_Kneel_Rise.GA_EredrimOnly_Kneel_Rise_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Eredrim_ExamineKneel_Loop.GA_Eredrim_ExamineKneel_Loop_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Eredrim_ExamineKneel_Rise.GA_Eredrim_ExamineKneel_Rise_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Eredrim_InsistDrink.GA_Eredrim_InsistDrink_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Eredrim_Kneel_Rise.GA_Eredrim_Kneel_Rise_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Eredrim_Kneel_Stationary.GA_Eredrim_Kneel_Stationary_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Eredrim_OfferCoinPouch.GA_Eredrim_OfferCoinPouch_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Eredrim_OfferCoinPouchLoop.GA_Eredrim_OfferCoinPouchLoop_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Eredrim_OfferDrink.GA_Eredrim_OfferDrink_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Eredrim_PullOutSword.GA_Eredrim_PullOutSword_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Eredrim_TakeAwayCoinPouch.GA_Eredrim_TakeAwayCoinPouch_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Generic_Kneel_Loop.GA_Generic_Kneel_Loop_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Generic_TakeFromNPC.GA_Generic_TakeFromNPC_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Genessa_Kneel_Rise.GA_Genessa_Kneel_Rise_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Genessa_PrayerStandUp.GA_Genessa_PrayerStandUp_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Genessa_RaiseCenser.GA_Genessa_RaiseCenser_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Genessa_RealeaseSesters.GA_Genessa_RealeaseSesters_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Genessa_Reunite.GA_Genessa_Reunite_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Genessa_Standing_Idle.GA_Genessa_Standing_Idle_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Genessa_TossCenser.GA_Genessa_TossCenser_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Gragu_DiePoison.GA_Gragu_DiePoison_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Gragu_DrinkCup.GA_Gragu_DrinkCup_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Gragu_DrinkPoison.GA_Gragu_DrinkPoison_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Gragu_PassOutAfterPuke.GA_Gragu_PassOutAfterPuke_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Gragu_PoisonedSitting.GA_Gragu_PoisonedSitting_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Gragu_StepOnTemp.GA_Gragu_StepOnTemp_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Gragu_TakeSeat.GA_Gragu_TakeSeat_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Gragu_TavernSitLoop.GA_Gragu_TavernSitLoop_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_InteractionMenuBase.GA_InteractionMenuBase_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | yes (count 1) |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Lazlo_PourGloomFirst.GA_Lazlo_PourGloomFirst_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Lazlo_StepBackReactNormal.GA_Lazlo_StepBackReactNormal_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Lazlo_TakeMarrusMask.GA_Lazlo_TakeMarrusMask_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Lazlo_TakeMarrusSword.GA_Lazlo_TakeMarrusSword_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Lazlo_ThrowGloom.GA_Lazlo_ThrowGloom_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_AngryEmote_01.GA_Player_AngryEmote_01_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_AngryEmote_02.GA_Player_AngryEmote_02_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_BendDownReach.GA_Player_BendDownReach_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_BossSiphon_Interaction.GA_Player_BossSiphon_Interaction_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_ChaliceInteract.GA_Player_ChaliceInteract_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_ChaliceInteract_Eredrim.GA_Player_ChaliceInteract_Eredrim_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_ClaimCenser_Genessa_Memory.GA_Player_ClaimCenser_Genessa_Memory_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_CoinFlip_Interact.GA_Player_CoinFlip_Interact_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_Beckon.GA_Player_DialogueEmote_Beckon_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_Disagree.GA_Player_DialogueEmote_Disagree_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_ExamineArmor.GA_Player_DialogueEmote_ExamineArmor_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_Gesture04.GA_Player_DialogueEmote_Gesture04_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_HoldTorch.GA_Player_DialogueEmote_HoldTorch_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_IdleCancelLoop.GA_Player_DialogueEmote_IdleCancelLoop_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_Lazlo_Explain.GA_Player_DialogueEmote_Lazlo_Explain_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_LookAround.GA_Player_DialogueEmote_LookAround_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_LookAroundFrantic.GA_Player_DialogueEmote_LookAroundFrantic_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_LookAroundUp.GA_Player_DialogueEmote_LookAroundUp_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_NoMercy.GA_Player_DialogueEmote_NoMercy_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_PointFinger_MegaFast.GA_Player_DialogueEmote_PointFinger_MegaFast_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_Proclaim.GA_Player_DialogueEmote_Proclaim_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_Question.GA_Player_DialogueEmote_Question_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_SlapHitReaction.GA_Player_DialogueEmote_SlapHitReaction_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_Stomp_Eredrim.GA_Player_DialogueEmote_Stomp_Eredrim_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_TurnBack_180.GA_Player_DialogueEmote_TurnBack_180_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_WalkBack.GA_Player_DialogueEmote_WalkBack_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_WalkFront.GA_Player_DialogueEmote_WalkFront_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_WalkFront2.GA_Player_DialogueEmote_WalkFront2_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_WalkFront_Slower.GA_Player_DialogueEmote_WalkFront_Slower_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DrinkWhileSitting.GA_Player_DrinkWhileSitting_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_FastPunch.GA_Player_FastPunch_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_FastPunch_Left.GA_Player_FastPunch_Left_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_FlyBack.GA_Player_FlyBack_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_FlyBack_End_Sariel.GA_Player_FlyBack_End_Sariel_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_FlyBack_Sariel.GA_Player_FlyBack_Sariel_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_Flyback_Weak.GA_Player_Flyback_Weak_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_GetHitToKneel.GA_Player_GetHitToKneel_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_GetHit_Memory.GA_Player_GetHit_Memory_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_GetPossessed_End.GA_Player_GetPossessed_End_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_GetPossessed_Loop.GA_Player_GetPossessed_Loop_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_GetUp_FromLayingOnBack.GA_Player_GetUp_FromLayingOnBack_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_GiveBall_Lazlo_Memory.GA_Player_GiveBall_Lazlo_Memory_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_GiveCoins.GA_Player_GiveCoins_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_HitDown.GA_Player_HitDown_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_IntenseRemark.GA_Player_IntenseRemark_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_IntenseRemarkSlow.GA_Player_IntenseRemarkSlow_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_KickObject.GA_Player_KickObject_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_KneelAndReach.GA_Player_KneelAndReach_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_KneelAndReach_Memory.GA_Player_KneelAndReach_Memory_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_KneelLoop_Slower.GA_Player_KneelLoop_Slower_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_KneelLoop_SlowerEredrim.GA_Player_KneelLoop_SlowerEredrim_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_KneelLoop_SlowerGenessa.GA_Player_KneelLoop_SlowerGenessa_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_KneelLoop_SlowerSmert.GA_Player_KneelLoop_SlowerSmert_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_KneelLoop_Slower_YoungLazlo.GA_Player_KneelLoop_Slower_YoungLazlo_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_KneelRise_Slower.GA_Player_KneelRise_Slower_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_KneelRise_SlowerEredrim.GA_Player_KneelRise_SlowerEredrim_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_KneelRise_SlowerSmert.GA_Player_KneelRise_SlowerSmert_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_Kneel_Loop.GA_Player_Kneel_Loop_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_KnockOnDoor_2Knocks.GA_Player_KnockOnDoor_2Knocks_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_KnockedBack_KneelRise.GA_Player_KnockedBack_KneelRise_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_Lazlo_SharpDisagree.GA_Player_Lazlo_SharpDisagree_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_Lazlo_Strike.GA_Player_Lazlo_Strike_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_OpenDoor.GA_Player_OpenDoor_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_PickUpBigJar.GA_Player_PickUpBigJar_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_PickUpBigMug.GA_Player_PickUpBigMug_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_PourGloom.GA_Player_PourGloom_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_Pray.GA_Player_Pray_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_Pray_Tiel.GA_Player_Pray_Tiel_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_Prologue_Arise.GA_Player_Prologue_Arise_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_Prologue_EggStrandingCustom.GA_Player_Prologue_EggStrandingCustom_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | yes (count 6) |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_Prologue_Getup_After_Explosion.GA_Player_Prologue_Getup_After_Explosion_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | yes (count 1) |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_Puke.GA_Player_Puke_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_PushUpFromGround.GA_Player_PushUpFromGround_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_ReachOut.GA_Player_ReachOut_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_ReachUp.GA_Player_ReachUp_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_ReachesForward.GA_Player_ReachesForward_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_RelaxInCauldron.GA_Player_RelaxInCauldron_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_RiseFromFaceDown.GA_Player_RiseFromFaceDown_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_RiseFromKneel.GA_Player_RiseFromKneel_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_RitualPalmSlice_Interact.GA_Player_RitualPalmSlice_Interact_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_RitualPalmSlice_Interact_Sariel.GA_Player_RitualPalmSlice_Interact_Sariel_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_RitualSnap.GA_Player_RitualSnap_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_SarielTakeScythe_Memory.GA_Player_SarielTakeScythe_Memory_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_SexyAuntyCapsule_Interaction.GA_Player_SexyAuntyCapsule_Interaction_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_SitDown.GA_Player_SitDown_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_SitThenLoop_Vista.GA_Player_SitThenLoop_Vista_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 8) |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_SitThenLoop_WithCancel.GA_Player_SitThenLoop_WithCancel_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | yes (count 7) |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_Sleep.GA_Player_Sleep_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_StandFromSit.GA_Player_StandFromSit_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_StandFromSleep.GA_Player_StandFromSleep_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_StandFromSleep_Loop.GA_Player_StandFromSleep_Loop_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_StandUp.GA_Player_StandUp_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_StepBack_Longer.GA_Player_StepBack_Longer_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_StepOutofShell.GA_Player_StepOutofShell_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | yes (count 1) |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_StowGun.GA_Player_StowGun_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_StowToBack.GA_Player_StowToBack_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_Suck.GA_Player_Suck_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_TakeBall_Lazlo_Memory.GA_Player_TakeBall_Lazlo_Memory_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_TakeCoinNoHide_Lazlo_Memory.GA_Player_TakeCoinNoHide_Lazlo_Memory_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_TakeCoinPutBack.GA_Player_TakeCoinPutBack_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_TakeLetter_Lazlo_Memory.GA_Player_TakeLetter_Lazlo_Memory_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_TakeTorch.GA_Player_TakeTorch_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_TakeTorch_Genessa_Memory.GA_Player_TakeTorch_Genessa_Memory_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_TielGrabKnife_Memory.GA_Player_TielGrabKnife_Memory_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_UnstoweFromBack.GA_Player_UnstoweFromBack_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_WalkBackSlow.GA_Player_WalkBackSlow_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_WeakenedKneel_Loop.GA_Player_WeakenedKneel_Loop_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Player_WideArmsAround.GA_Player_WideArmsAround_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Proxima_OnGround.GA_Proxima_OnGround_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Proxima_OnGround_Arrival.GA_Proxima_OnGround_Arrival_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Proxima_PickUpMachinegun.GA_Proxima_PickUpMachinegun_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Proxima_Standup.GA_Proxima_Standup_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_SarielRessurection.GA_SarielRessurection_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Sariel_FallDown.GA_Sariel_FallDown_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Sariel_InjuredLaying.GA_Sariel_InjuredLaying_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Sariel_InjuredSitUp.GA_Sariel_InjuredSitUp_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Sariel_LayOnGroundWeakly.GA_Sariel_LayOnGroundWeakly_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Sariel_WriggleOnGround.GA_Sariel_WriggleOnGround_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Smert_Collapse.GA_Smert_Collapse_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Smert_Flyback.GA_Smert_Flyback_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Smert_KneelPraise.GA_Smert_KneelPraise_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Smert_PunchTree_L.GA_Smert_PunchTree_L_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Smert_PunchTree_R.GA_Smert_PunchTree_R_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Smert_Punch_Memory.GA_Smert_Punch_Memory_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Smert_SitInjured.GA_Smert_SitInjured_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Smert_SitInjured_StandUp.GA_Smert_SitInjured_StandUp_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_StandUpFromKneel.GA_StandUpFromKneel_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Tiel_KickBottles.GA_Tiel_KickBottles_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Tiel_KneelLoop1.GA_Tiel_KneelLoop1_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Tiel_MysteriousBottle_TM1_S1.GA_Tiel_MysteriousBottle_TM1_S1_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Tiel_Sleeping.GA_Tiel_Sleeping_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Tiel_Sleeping_TheCure2.GA_Tiel_Sleeping_TheCure2_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Tiel_Sleeping_Wiggling.GA_Tiel_Sleeping_Wiggling_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Tiel_StandingSad.GA_Tiel_StandingSad_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Tiel_StartKneel.GA_Tiel_StartKneel_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Tiel_StepBackAfterDestruction.GA_Tiel_StepBackAfterDestruction_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Tiel_ThrowRock.GA_Tiel_ThrowRock_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/GA_Warden_Disappointed.GA_Warden_Disappointed_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Eredrim_Falls_PeaceAtLast.GA_Eredrim_Falls_PeaceAtLast_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Eredrim_GroundPose.GA_Eredrim_GroundPose_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Eredrim_StandupFromGround.GA_Eredrim_StandupFromGround_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Pained_GroundLoop.GA_Pained_GroundLoop_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Pained_GroundPose.GA_Pained_GroundPose_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Player_Examine_Kneel_Smert.GA_Player_Examine_Kneel_Smert_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Player_GenericExamine_Kneel.GA_Player_GenericExamine_Kneel_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Player_GenericExamine_Rise.GA_Player_GenericExamine_Rise_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Player_ProximaBiosamplerExamine.GA_Player_ProximaBiosamplerExamine_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Sariel_CutDarkOvum.GA_Sariel_CutDarkOvum_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Smert_ReactToPunch_L.GA_Smert_ReactToPunch_L_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Smert_ReactToPunch_Mid.GA_Smert_ReactToPunch_Mid_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Smert_ReactToPunch_R.GA_Smert_ReactToPunch_R_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Tiel_JumpWindow.GA_Tiel_JumpWindow_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Tiel_TakeEmptyBottle.GA_Tiel_TakeEmptyBottle_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Tiel_TakesBottle_TM6_S1.GA_Tiel_TakesBottle_TM6_S1_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Tiel_ThreatenApothecary.GA_Tiel_ThreatenApothecary_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Tiel_WindowFail.GA_Tiel_WindowFail_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Tiel_WindowOpen.GA_Tiel_WindowOpen_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |

Evidence - interaction ability class rows (all 193):
```
[000001F617FA98B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_InteractionMenuBase.GA_InteractionMenuBase_C [n: 88F38] [c: 00007FF46EEC3D38] [or: 000001F4D1C1AD80] [sps: 000001F615242DD0]
[000001F617FA86E0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_Prologue_EggStrandingCustom.GA_Player_Prologue_EggStrandingCustom_C [n: 299EB9] [c: 00007FF46EEC3D38] [or: 000001F4D1C1ABF0] [sps: 000001F615242DD0]
[000001F7498FA360] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Tiel_StandingSad.GA_Tiel_StandingSad_C [n: 1027C2] [c: 00007FF46EEC3D38] [or: 000001F76386CA90] [sps: 000001F615242DD0]
[000001F76C15F1C0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_KnockedBack_KneelRise.GA_Player_KnockedBack_KneelRise_C [n: CEB0D] [c: 00007FF46EEC3D38] [or: 000001F76386C400] [sps: 000001F615242DD0]
[000001F76D649FD0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_IdleCancelLoop.GA_Player_DialogueEmote_IdleCancelLoop_C [n: 36EBE1] [c: 00007FF46EEC3D38] [or: 000001F76386B6E0] [sps: 000001F615242DD0]
[000001F76D890DC0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_StowToBack.GA_Player_StowToBack_C [n: 11CE55] [c: 00007FF46EEC3D38] [or: 000001F76386B370] [sps: 000001F615242DD0]
[000001F76D89B1A0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_UnstoweFromBack.GA_Player_UnstoweFromBack_C [n: 331F6F] [c: 00007FF46EEC3D38] [or: 000001F76386B0F0] [sps: 000001F615242DD0]
[000001F8921A9520] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Genessa_Standing_Idle.GA_Genessa_Standing_Idle_C [n: 46B3D7] [c: 00007FF46EEC3D38] [or: 000001F8921E0150] [sps: 000001F615242DD0]
[000001F8A77B2DD0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_Stomp_Eredrim.GA_Player_DialogueEmote_Stomp_Eredrim_C [n: 12E2C3] [c: 00007FF46EEC3D38] [or: 000001F8A0B48440] [sps: 000001F615242DD0]
[000001F8A77BB1A0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_Lazlo_Explain.GA_Player_DialogueEmote_Lazlo_Explain_C [n: 22CFEF] [c: 00007FF46EEC3D38] [or: 000001F8A0B493E0] [sps: 000001F615242DD0]
[000001F8A77BA6F0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_GiveBall_Lazlo_Memory.GA_Player_GiveBall_Lazlo_Memory_C [n: 32940C] [c: 00007FF46EEC3D38] [or: 000001F8A0B49750] [sps: 000001F615242DD0]
[000001F8920BCE20] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_KneelAndReach.GA_Player_KneelAndReach_C [n: 4B0C44] [c: 00007FF46EEC3D38] [or: 000001F8A0B49390] [sps: 000001F615242DD0]
[000001F8920BB1A0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_TakeBall_Lazlo_Memory.GA_Player_TakeBall_Lazlo_Memory_C [n: 25903E] [c: 00007FF46EEC3D38] [or: 000001F8A0B49430] [sps: 000001F615242DD0]
[000001F8920BAE10] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_ReachUp.GA_Player_ReachUp_C [n: 2DAB92] [c: 00007FF46EEC3D38] [or: 000001F8A0B4EC00] [sps: 000001F615242DD0]
[000001F8B0F1F550] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_RelaxInCauldron.GA_Player_RelaxInCauldron_C [n: E9326] [c: 00007FF46EEC3D38] [or: 000001F893C1B370] [sps: 000001F615242DD0]
[000001F8B6977510] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_CoinFlip_Interact.GA_Player_CoinFlip_Interact_C [n: 443C41] [c: 00007FF46EEC3D38] [or: 000001F893C1D760] [sps: 000001F615242DD0]
[000001F8ABC51C00] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_TakeTorch.GA_Player_TakeTorch_C [n: 2B4364] [c: 00007FF46EEC3D38] [or: 000001F8C88B92A0] [sps: 000001F615242DD0]
[000001F8BC46C370] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_RitualSnap.GA_Player_RitualSnap_C [n: E4B1C] [c: 00007FF46EEC3D38] [or: 000001F8B6A255B0] [sps: 000001F615242DD0]
[000001F8C2C84330] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_FlyBack.GA_Player_FlyBack_C [n: 228AD0] [c: 00007FF46EEC3D38] [or: 000001F8B6A2E3E0] [sps: 000001F615242DD0]
[000001F8C2C83880] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_SexyAuntyCapsule_Interaction.GA_Player_SexyAuntyCapsule_Interaction_C [n: FE36D] [c: 00007FF46EEC3D38] [or: 000001F8C9C90420] [sps: 000001F615242DD0]
[000001F8C2C83160] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_ExamineArmor.GA_Player_DialogueEmote_ExamineArmor_C [n: 21BA77] [c: 00007FF46EEC3D38] [or: 000001F8C9C903D0] [sps: 000001F615242DD0]
[000001F8BC46AE10] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_TakeLetter_Lazlo_Memory.GA_Player_TakeLetter_Lazlo_Memory_C [n: 29DEB1] [c: 00007FF46EEC3D38] [or: 000001F8CAED0100] [sps: 000001F615242DD0]
[000001F8BC46B530] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_Lazlo_SharpDisagree.GA_Player_Lazlo_SharpDisagree_C [n: 358864] [c: 00007FF46EEC3D38] [or: 000001F8CAED00B0] [sps: 000001F615242DD0]
[000001F7ED2C46C0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_SitThenLoop_Vista.GA_Player_SitThenLoop_Vista_C [n: 2DAB9D] [c: 00007FF46EEC3D38] [or: 000001F7ED272FE0] [sps: 000001F4DC018E00]
[000001F7ED2C26B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_ChaliceInteract_Eredrim.GA_Player_ChaliceInteract_Eredrim_C [n: 2A6D30] [c: 00007FF46EEC3D38] [or: 000001F7ED2745C0] [sps: 000001F615242DD0]
[000001F7ED2C1C00] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_EredrimOnly_ExamineKneel_Loop.GA_EredrimOnly_ExamineKneel_Loop_C [n: 417231] [c: 00007FF46EEC3D38] [or: 000001F7ED2740C0] [sps: 000001F615242DD0]
[000001F7ED2C06A0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_KneelLoop_SlowerEredrim.GA_Player_KneelLoop_SlowerEredrim_C [n: 88F45] [c: 00007FF46EEC3D38] [or: 000001F7ED274B60] [sps: 000001F615242DD0]
[000001F7ED2CE380] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Eredrim_Kneel_Stationary.GA_Eredrim_Kneel_Stationary_C [n: E4B0C] [c: 00007FF46EEC3D38] [or: 000001F7ED276D20] [sps: 000001F615242DD0]
[000001F7ED2CDC60] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Eredrim_PullOutSword.GA_Eredrim_PullOutSword_C [n: 129F39] [c: 00007FF46EEC3D38] [or: 000001F7ED276CD0] [sps: 000001F615242DD0]
[000001F7ED2CD540] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Eredrim_Kneel_Rise.GA_Eredrim_Kneel_Rise_C [n: 2F93C0] [c: 00007FF46EEC3D38] [or: 000001F7ED276AF0] [sps: 000001F615242DD0]
[000001F7ED2CE710] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Eredrim_StandupFromGround.GA_Eredrim_StandupFromGround_C [n: 65F43] [c: 00007FF46EEC3D38] [or: 000001F7ED278F80] [sps: 000001F615242DD0]
[000001F7ED2C8350] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_ClaimCenser_Genessa_Memory.GA_Player_ClaimCenser_Genessa_Memory_C [n: 3B4B8A] [c: 00007FF46EEC3D38] [or: 000001F7ED278A30] [sps: 000001F615242DD0]
[000001F8CE7CC700] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Genessa_Kneel_Rise.GA_Genessa_Kneel_Rise_C [n: 1ECA74] [c: 00007FF46EEC3D38] [or: 000001F7ED278A80] [sps: 000001F615242DD0]
[000001F8CE7CBFE0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_KneelLoop_SlowerGenessa.GA_Player_KneelLoop_SlowerGenessa_C [n: 3C72F3] [c: 00007FF46EEC3D38] [or: 000001F7ED278AD0] [sps: 000001F615242DD0]
[000001F8BC468E00] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_Pray.GA_Player_Pray_C [n: 3C2A0C] [c: 00007FF46EEC3D38] [or: 000001F7ED27ABF0] [sps: 000001F615242DD0]
[000001F8BC469190] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Genessa_PrayerStandUp.GA_Genessa_PrayerStandUp_C [n: 265E3A] [c: 00007FF46EEC3D38] [or: 000001F7ED279070] [sps: 000001F615242DD0]
[000001F8CE8D2A40] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_OpenDoor.GA_Player_OpenDoor_C [n: 88F58] [c: 00007FF46EEC3D38] [or: 000001F8CEA11730] [sps: 000001F615242DD0]
[000001F8CE8D8A70] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Genessa_RaiseCenser.GA_Genessa_RaiseCenser_C [n: 39A74A] [c: 00007FF46EEC3D38] [or: 000001F8CEA16AF0] [sps: 000001F615242DD0]
[000001F8CE8D7FC0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Genessa_Reunite.GA_Genessa_Reunite_C [n: 16E8D2] [c: 00007FF46EEC3D38] [or: 000001F8CEA16A50] [sps: 000001F615242DD0]
[000001F8CEEE1F90] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_WalkFront_Slower.GA_Player_DialogueEmote_WalkFront_Slower_C [n: 22D004] [c: 00007FF46EEC3D38] [or: 000001F8CEA18620] [sps: 000001F615242DD0]
[000001F8CEEE0DC0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Genessa_RealeaseSesters.GA_Genessa_RealeaseSesters_C [n: 2BCDCE] [c: 00007FF46EEC3D38] [or: 000001F8CEA1AD30] [sps: 000001F615242DD0]
[000001F8CEEE06A0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Genessa_TossCenser.GA_Genessa_TossCenser_C [n: 1C9613] [c: 00007FF46EEC3D38] [or: 000001F8CEA1AD80] [sps: 000001F615242DD0]
[000001F8C2C8CE20] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_Suck.GA_Player_Suck_C [n: 4594F9] [c: 00007FF46EEC3D38] [or: 000001F8CEA1DEE0] [sps: 000001F615242DD0]
[000001F7498F3FA0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_SlapHitReaction.GA_Player_DialogueEmote_SlapHitReaction_C [n: C2070] [c: 00007FF46EEC3D38] [or: 000001F8CEA1DE40] [sps: 000001F615242DD0]
[000001F756AC1F90] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_HoldTorch.GA_Player_DialogueEmote_HoldTorch_C [n: 183AE9] [c: 00007FF46EEC3D38] [or: 000001F8CEA1DDF0] [sps: 000001F615242DD0]
[000001F4ADE24330] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_WalkFront.GA_Player_DialogueEmote_WalkFront_C [n: 2915ED] [c: 00007FF46EEC3D38] [or: 000001F8CEA1DDA0] [sps: 000001F615242DD0]
[000001F8BC468A70] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_WalkBackSlow.GA_Player_WalkBackSlow_C [n: 3A3468] [c: 00007FF46EEC3D38] [or: 000001F8CEA1ECF0] [sps: 000001F615242DD0]
[000001F4ADE22A40] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_ReachesForward.GA_Player_ReachesForward_C [n: 272A7C] [c: 00007FF46EEC3D38] [or: 000001F8CEA1ECA0] [sps: 000001F615242DD0]
[000001F8C2C8BC50] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_TakeTorch_Genessa_Memory.GA_Player_TakeTorch_Genessa_Memory_C [n: ED612] [c: 00007FF46EEC3D38] [or: 000001F8CEA1C950] [sps: 000001F615242DD0]
[000001F8BC467FC0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Gragu_DrinkPoison.GA_Gragu_DrinkPoison_C [n: 154BCF] [c: 00007FF46EEC3D38] [or: 000001F8CF1B8530] [sps: 000001F615242DD0]
[000001F8BC4686E0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_PointFinger_MegaFast.GA_Player_DialogueEmote_PointFinger_MegaFast_C [n: 183AFC] [c: 00007FF46EEC3D38] [or: 000001F8CF1B84E0] [sps: 000001F615242DD0]
[000001F8CEEEAE10] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Gragu_DiePoison.GA_Gragu_DiePoison_C [n: 6A578] [c: 00007FF46EEC3D38] [or: 000001F8CF1B8440] [sps: 000001F615242DD0]
[000001F8CEEE7FC0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Gragu_PassOutAfterPuke.GA_Gragu_PassOutAfterPuke_C [n: E0407] [c: 00007FF46EEC3D38] [or: 000001F8CF1B74A0] [sps: 000001F615242DD0]
[000001F8CEEE78A0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Gragu_StepOnTemp.GA_Gragu_StepOnTemp_C [n: 2B86D3] [c: 00007FF46EEC3D38] [or: 000001F8CF1B79A0] [sps: 000001F615242DD0]
[000001F8CF827180] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_Puke.GA_Player_Puke_C [n: 299ECE] [c: 00007FF46EEC3D38] [or: 000001F8CF1B6910] [sps: 000001F615242DD0]
[000001F8CF826A60] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_RiseFromFaceDown.GA_Player_RiseFromFaceDown_C [n: 3B988A] [c: 00007FF46EEC3D38] [or: 000001F8CF1B6730] [sps: 000001F615242DD0]
[000001F8CEEE8350] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_GiveCoins.GA_Player_GiveCoins_C [n: 429287] [c: 00007FF46EEC3D38] [or: 000001F8CF1B9E30] [sps: 000001F615242DD0]
[000001F8CF829520] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Gragu_DrinkCup.GA_Gragu_DrinkCup_C [n: 4BD954] [c: 00007FF46EEC3D38] [or: 000001F8CF1BCEA0] [sps: 000001F615242DD0]
[000001F8CF8286E0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Gragu_TakeSeat.GA_Gragu_TakeSeat_C [n: 361751] [c: 00007FF46EEC3D38] [or: 000001F8CF1BFB50] [sps: 000001F615242DD0]
[000001F8CFAF3FA0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_PickUpBigJar.GA_Player_PickUpBigJar_C [n: 400E82] [c: 00007FF46EEC3D38] [or: 000001F8CF997540] [sps: 000001F615242DD0]
[000001F8CFAF3160] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_PickUpBigMug.GA_Player_PickUpBigMug_C [n: 7BEAA] [c: 00007FF46EEC3D38] [or: 000001F8CF9975E0] [sps: 000001F615242DD0]
[000001F8CFAF1F90] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_KickObject.GA_Player_KickObject_C [n: 2DF014] [c: 00007FF46EEC3D38] [or: 000001F8CF997C20] [sps: 000001F615242DD0]
[000001F8CFAF0DC0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_PourGloom.GA_Player_PourGloom_C [n: 302A71] [c: 00007FF46EEC3D38] [or: 000001F8CF997E00] [sps: 000001F615242DD0]
[000001F8CFAFD1B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Lazlo_ThrowGloom.GA_Lazlo_ThrowGloom_C [n: 3D386C] [c: 00007FF46EEC3D38] [or: 000001F8CF99C090] [sps: 000001F615242DD0]
[000001F8CFAFCA90] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Lazlo_PourGloomFirst.GA_Lazlo_PourGloomFirst_C [n: 3FC853] [c: 00007FF46EEC3D38] [or: 000001F8CF99C040] [sps: 000001F615242DD0]
[000001F8CFAFB530] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Lazlo_TakeMarrusMask.GA_Lazlo_TakeMarrusMask_C [n: 17F2AB] [c: 00007FF46EEC3D38] [or: 000001F8CF99EC00] [sps: 000001F615242DD0]
[000001F8CFAF98B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Lazlo_TakeMarrusSword.GA_Lazlo_TakeMarrusSword_C [n: 19A034] [c: 00007FF46EEC3D38] [or: 000001F8CF99E610] [sps: 000001F615242DD0]
[000001F8CFAF8A70] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_TakeCoinNoHide_Lazlo_Memory.GA_Player_TakeCoinNoHide_Lazlo_Memory_C [n: 2AB4C0] [c: 00007FF46EEC3D38] [or: 000001F8CF99F5B0] [sps: 000001F615242DD0]
[000001F8C61FDC60] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Eredrim_ExamineKneel_Loop.GA_Eredrim_ExamineKneel_Loop_C [n: 48D885] [c: 00007FF46EEC3D38] [or: 000001F8C62B1870] [sps: 000001F615242DD0]
[000001F8C61FCA90] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_FastPunch.GA_Player_FastPunch_C [n: 213AA6] [c: 00007FF46EEC3D38] [or: 000001F8C62B17D0] [sps: 000001F615242DD0]
[000001F8C61FBC50] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_StowGun.GA_Player_StowGun_C [n: 29DEA6] [c: 00007FF46EEC3D38] [or: 000001F8C62B1690] [sps: 000001F615242DD0]
[000001F8C61FB530] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Player_GenericExamine_Rise.GA_Player_GenericExamine_Rise_C [n: 3F3662] [c: 00007FF46EEC3D38] [or: 000001F8C62B1500] [sps: 000001F615242DD0]
[000001F8CEEEF550] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Proxima_PickUpMachinegun.GA_Proxima_PickUpMachinegun_C [n: 3071D0] [c: 00007FF46EEC3D38] [or: 000001F8C62B15F0] [sps: 000001F615242DD0]
[000001F8C61FE380] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Proxima_OnGround.GA_Proxima_OnGround_C [n: 1C546A] [c: 00007FF46EEC3D38] [or: 000001F8C62B1E10] [sps: 000001F615242DD0]
[000001F8C65D3160] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Eredrim_ExamineKneel_Rise.GA_Eredrim_ExamineKneel_Rise_C [n: 2F93B0] [c: 00007FF46EEC3D38] [or: 000001F8C62B62D0] [sps: 000001F615242DD0]
[000001F8C65D26B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Player_GenericExamine_Kneel.GA_Player_GenericExamine_Kneel_C [n: 45070F] [c: 00007FF46EEC3D38] [or: 000001F8C62B1D70] [sps: 000001F615242DD0]
[000001F8C61F6A60] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_NoMercy.GA_Player_DialogueEmote_NoMercy_C [n: 25443D] [c: 00007FF46EEC3D38] [or: 000001F8C62B99D0] [sps: 000001F615242DD0]
[000001F8C65D9FD0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Gragu_PoisonedSitting.GA_Gragu_PoisonedSitting_C [n: 2F4D0C] [c: 00007FF46EEC3D38] [or: 000001F8C62BB050] [sps: 000001F615242DD0]
[000001F8C65DFC70] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_KneelRise_Slower.GA_Player_KneelRise_Slower_C [n: 31C942] [c: 00007FF46EEC3D38] [or: 000001F8C62BE020] [sps: 000001F615242DD0]
[000001F8C65DF550] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_KneelLoop_Slower.GA_Player_KneelLoop_Slower_C [n: 107057] [c: 00007FF46EEC3D38] [or: 000001F8C62BDFD0] [sps: 000001F615242DD0]
[000001F8C65DEE30] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_SarielRessurection.GA_SarielRessurection_C [n: 4325CF] [c: 00007FF46EEC3D38] [or: 000001F8C62BDF80] [sps: 000001F615242DD0]
[000001F8D0B766D0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_RitualPalmSlice_Interact_Sariel.GA_Player_RitualPalmSlice_Interact_Sariel_C [n: CEB1F] [c: 00007FF46EEC3D38] [or: 000001F8C62BDDA0] [sps: 000001F615242DD0]
[000001F8D0B75500] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_BendDownReach.GA_Player_BendDownReach_C [n: 4AC579] [c: 00007FF46EEC3D38] [or: 000001F8C62BDD00] [sps: 000001F615242DD0]
[000001F8C61F66D0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_WalkBack.GA_Player_DialogueEmote_WalkBack_C [n: 2A279D] [c: 00007FF46EEC3D38] [or: 000001F8D0884110] [sps: 000001F615242DD0]
[000001F8C61F5500] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_ReachOut.GA_Player_ReachOut_C [n: 1AB59E] [c: 00007FF46EEC3D38] [or: 000001F8D0888DF0] [sps: 000001F615242DD0]
[000001F8C61F4330] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Sariel_FallDown.GA_Sariel_FallDown_C [n: 195B09] [c: 00007FF46EEC3D38] [or: 000001F8D088B9B0] [sps: 000001F615242DD0]
[000001F8C61F4A50] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_KneelAndReach_Memory.GA_Player_KneelAndReach_Memory_C [n: 125CE1] [c: 00007FF46EEC3D38] [or: 000001F8D088CE00] [sps: 000001F615242DD0]
[000001F8C61F4DE0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_LookAroundFrantic.GA_Player_DialogueEmote_LookAroundFrantic_C [n: 19123E] [c: 00007FF46EEC3D38] [or: 000001F8D088CDB0] [sps: 000001F615242DD0]
[000001F8D0FD4DE0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Sariel_InjuredLaying.GA_Sariel_InjuredLaying_C [n: 2D6A27] [c: 00007FF46EEC3D38] [or: 000001F8D088CC20] [sps: 000001F615242DD0]
[000001F8D0FD46C0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_StandFromSit.GA_Player_StandFromSit_C [n: 19128D] [c: 00007FF46EEC3D38] [or: 000001F8D088CBD0] [sps: 000001F615242DD0]
[000001F8D0FD3FA0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Sariel_WriggleOnGround.GA_Sariel_WriggleOnGround_C [n: 96256] [c: 00007FF46EEC3D38] [or: 000001F8D088CAE0] [sps: 000001F615242DD0]
[000001F8D0FD0310] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Smert_ReactToPunch_L.GA_Smert_ReactToPunch_L_C [n: F19C4] [c: 00007FF46EEC3D38] [or: 000001F8D088FA10] [sps: 000001F615242DD0]
[000001F8CEEE5890] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Smert_Punch_Memory.GA_Smert_Punch_Memory_C [n: A760A] [c: 00007FF46EEC3D38] [or: 000001F8D088F9C0] [sps: 000001F615242DD0]
[000001F8CEEE5C20] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Smert_ReactToPunch_Mid.GA_Smert_ReactToPunch_Mid_C [n: E4B28] [c: 00007FF46EEC3D38] [or: 000001F8D088F970] [sps: 000001F615242DD0]
[000001F8CEEE6340] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_Flyback_Weak.GA_Player_Flyback_Weak_C [n: 32557A] [c: 00007FF46EEC3D38] [or: 000001F8D088F8D0] [sps: 000001F615242DD0]
[000001F8CE8D9190] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Smert_ReactToPunch_R.GA_Smert_ReactToPunch_R_C [n: 1027D9] [c: 00007FF46EEC3D38] [or: 000001F8D088F830] [sps: 000001F615242DD0]
[000001F8CE8DB1A0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Generic_Kneel_Loop.GA_Generic_Kneel_Loop_C [n: 49F32A] [c: 00007FF46EEC3D38] [or: 000001F8D088F880] [sps: 000001F615242DD0]
[000001F8CE8DCA90] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Player_Examine_Kneel_Smert.GA_Player_Examine_Kneel_Smert_C [n: 40E635] [c: 00007FF46EEC3D38] [or: 000001F8D088FA60] [sps: 000001F615242DD0]
[000001F8C61F3160] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_WideArmsAround.GA_Player_WideArmsAround_C [n: 2C0FED] [c: 00007FF46EEC3D38] [or: 000001F8D088F790] [sps: 000001F615242DD0]
[000001F8C61F2A40] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_PushUpFromGround.GA_Player_PushUpFromGround_C [n: 325587] [c: 00007FF46EEC3D38] [or: 000001F8D1262DB0] [sps: 000001F615242DD0]
[000001F8C61F26B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_IntenseRemark.GA_Player_IntenseRemark_C [n: 40E61B] [c: 00007FF46EEC3D38] [or: 000001F8D1262D10] [sps: 000001F615242DD0]
[000001F8D0FD0A30] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_WeakenedKneel_Loop.GA_Player_WeakenedKneel_Loop_C [n: 42DC6D] [c: 00007FF46EEC3D38] [or: 000001F8D1262CC0] [sps: 000001F615242DD0]
[000001F8C80C34F0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_GetHitToKneel.GA_Player_GetHitToKneel_C [n: 24780A] [c: 00007FF46EEC3D38] [or: 000001F8D1262C20] [sps: 000001F615242DD0]
[000001F8C80C0310] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Smert_Flyback.GA_Smert_Flyback_C [n: 37FD88] [c: 00007FF46EEC3D38] [or: 000001F8D1262BD0] [sps: 000001F615242DD0]
[000001F8C80CE380] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Smert_SitInjured.GA_Smert_SitInjured_C [n: 358875] [c: 00007FF46EEC3D38] [or: 000001F8D1262B80] [sps: 000001F615242DD0]
[000001F8ABC50DC0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Smert_SitInjured_StandUp.GA_Smert_SitInjured_StandUp_C [n: 213AB2] [c: 00007FF46EEC3D38] [or: 000001F8D1262C70] [sps: 000001F615242DD0]
[000001F8CB6306A0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Smert_PunchTree_R.GA_Smert_PunchTree_R_C [n: 36EC01] [c: 00007FF46EEC3D38] [or: 000001F8D1263F30] [sps: 000001F615242DD0]
[000001F8CB631870] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Smert_PunchTree_L.GA_Smert_PunchTree_L_C [n: 16A498] [c: 00007FF46EEC3D38] [or: 000001F8D1263EE0] [sps: 000001F615242DD0]
[000001F8CB632320] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Warden_Disappointed.GA_Warden_Disappointed_C [n: 1A7212] [c: 00007FF46EEC3D38] [or: 000001F8D1263E90] [sps: 000001F615242DD0]
[000001F8D0FDD1B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_TakeCoinPutBack.GA_Player_TakeCoinPutBack_C [n: 353C44] [c: 00007FF46EEC3D38] [or: 000001F8D12651F0] [sps: 000001F615242DD0]
[000001F8C61F1F90] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_Question.GA_Player_DialogueEmote_Question_C [n: 28D29B] [c: 00007FF46EEC3D38] [or: 000001F8D1263E40] [sps: 000001F615242DD0]
[000001F8CB630310] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Smert_Collapse.GA_Smert_Collapse_C [n: 284890] [c: 00007FF46EEC3D38] [or: 000001F8D126B820] [sps: 000001F615242DD0]
[000001F8D0FDEAA0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_GetHit_Memory.GA_Player_GetHit_Memory_C [n: 2240DE] [c: 00007FF46EEC3D38] [or: 000001F8D126B8C0] [sps: 000001F615242DD0]
[000001F8D1EF5C20] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_FastPunch_Left.GA_Player_FastPunch_Left_C [n: 2A27AF] [c: 00007FF46EEC3D38] [or: 000001F8D1264B10] [sps: 000001F615242DD0]
[000001F8CB6334F0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Lazlo_StepBackReactNormal.GA_Lazlo_StepBackReactNormal_C [n: 443C31] [c: 00007FF46EEC3D38] [or: 000001F8D126DAD0] [sps: 000001F615242DD0]
[000001F8D1EFAE10] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_HitDown.GA_Player_HitDown_C [n: 269F87] [c: 00007FF46EEC3D38] [or: 000001F8D2131730] [sps: 000001F615242DD0]
[000001F8D1EFA6F0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_GetPossessed_Loop.GA_Player_GetPossessed_Loop_C [n: 247818] [c: 00007FF46EEC3D38] [or: 000001F8D21316E0] [sps: 000001F615242DD0]
[000001F8D1EF9C40] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Smert_KneelPraise.GA_Smert_KneelPraise_C [n: 346AD7] [c: 00007FF46EEC3D38] [or: 000001F8D21315F0] [sps: 000001F615242DD0]
[000001F8D1EF9520] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_GetPossessed_End.GA_Player_GetPossessed_End_C [n: 15DB3B] [c: 00007FF46EEC3D38] [or: 000001F8D21315A0] [sps: 000001F615242DD0]
[000001F8D1EF8E00] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_RitualPalmSlice_Interact.GA_Player_RitualPalmSlice_Interact_C [n: 12E2E4] [c: 00007FF46EEC3D38] [or: 000001F8D2131500] [sps: 000001F615242DD0]
[000001F8D2267510] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Tiel_JumpWindow.GA_Tiel_JumpWindow_C [n: 2AFC1D] [c: 00007FF46EEC3D38] [or: 000001F8D2135B00] [sps: 000001F615242DD0]
[000001F8D2266DF0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Tiel_WindowFail.GA_Tiel_WindowFail_C [n: 96265] [c: 00007FF46EEC3D38] [or: 000001F8D2135A60] [sps: 000001F615242DD0]
[000001F8D22666D0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Tiel_ThreatenApothecary.GA_Tiel_ThreatenApothecary_C [n: 143DF9] [c: 00007FF46EEC3D38] [or: 000001F8D2135A10] [sps: 000001F615242DD0]
[000001F8D2265FB0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Tiel_WindowOpen.GA_Tiel_WindowOpen_C [n: 2B43BE] [c: 00007FF46EEC3D38] [or: 000001F8D21359C0] [sps: 000001F615242DD0]
[000001F8D2265890] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Tiel_TakeEmptyBottle.GA_Tiel_TakeEmptyBottle_C [n: 436D7E] [c: 00007FF46EEC3D38] [or: 000001F8D2135880] [sps: 000001F615242DD0]
[000001F8D22686E0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Tiel_StepBackAfterDestruction.GA_Tiel_StepBackAfterDestruction_C [n: 29DEC4] [c: 00007FF46EEC3D38] [or: 000001F8D21367D0] [sps: 000001F615242DD0]
[000001F8CA0B7180] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_StandUpFromKneel.GA_StandUpFromKneel_C [n: 2A2806] [c: 00007FF46EEC3D38] [or: 000001F8D213FA10] [sps: 000001F615242DD0]
[000001F8CA0B6A60] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_TielGrabKnife_Memory.GA_Player_TielGrabKnife_Memory_C [n: 84B69] [c: 00007FF46EEC3D38] [or: 000001F8D213FA60] [sps: 000001F615242DD0]
[000001F8CA0B6340] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_Pray_Tiel.GA_Player_Pray_Tiel_C [n: 12E2D8] [c: 00007FF46EEC3D38] [or: 000001F8D213FB00] [sps: 000001F615242DD0]
[000001F8D226FC70] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Tiel_KickBottles.GA_Tiel_KickBottles_C [n: 1A2B0E] [c: 00007FF46EEC3D38] [or: 000001F8D2136D70] [sps: 000001F615242DD0]
[000001F8CA0B1F90] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_StandFromSleep.GA_Player_StandFromSleep_C [n: 42DC5F] [c: 00007FF46EEC3D38] [or: 000001F634ED5240] [sps: 000001F615242DD0]
[000001F8CA0B1150] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_Sleep.GA_Player_Sleep_C [n: 25445A] [c: 00007FF46EEC3D38] [or: 000001F634ED51A0] [sps: 000001F615242DD0]
[000001F8CA0B0A30] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Tiel_Sleeping.GA_Tiel_Sleeping_C [n: 1F0CEE] [c: 00007FF46EEC3D38] [or: 000001F634ED5150] [sps: 000001F615242DD0]
[000001F8CA0B0310] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_StandFromSleep_Loop.GA_Player_StandFromSleep_Loop_C [n: 114820] [c: 00007FF46EEC3D38] [or: 000001F634ED5100] [sps: 000001F615242DD0]
[000001F8CA0BE710] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_LookAroundUp.GA_Player_DialogueEmote_LookAroundUp_C [n: 3A3454] [c: 00007FF46EEC3D38] [or: 000001F634ED50B0] [sps: 000001F615242DD0]
[000001F8CA0B98B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Tiel_ThrowRock.GA_Tiel_ThrowRock_C [n: 7FFCE] [c: 00007FF46EEC3D38] [or: 000001F634ED9020] [sps: 000001F615242DD0]
[000001F8CA0B86E0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Tiel_Sleeping_Wiggling.GA_Tiel_Sleeping_Wiggling_C [n: E045E] [c: 00007FF46EEC3D38] [or: 000001F634ED7E00] [sps: 000001F615242DD0]
[000001F8D73E2DD0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Eredrim_InsistDrink.GA_Eredrim_InsistDrink_C [n: D3085] [c: 00007FF46EEC3D38] [or: 000001F634EDC8B0] [sps: 000001F615242DD0]
[000001F8D73E26B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Eredrim_OfferCoinPouch.GA_Eredrim_OfferCoinPouch_C [n: 7FFB0] [c: 00007FF46EEC3D38] [or: 000001F634EDC810] [sps: 000001F615242DD0]
[000001F8D73E1F90] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Eredrim_OfferCoinPouchLoop.GA_Eredrim_OfferCoinPouchLoop_C [n: 4B0C18] [c: 00007FF46EEC3D38] [or: 000001F634EDC540] [sps: 000001F615242DD0]
[000001F8D73E1870] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Eredrim_OfferDrink.GA_Eredrim_OfferDrink_C [n: 150967] [c: 00007FF46EEC3D38] [or: 000001F634EDC400] [sps: 000001F615242DD0]
[000001F8D73E1150] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Eredrim_TakeAwayCoinPouch.GA_Eredrim_TakeAwayCoinPouch_C [n: 1F0CDE] [c: 00007FF46EEC3D38] [or: 000001F634EDD7B0] [sps: 000001F615242DD0]
[000001F8D73E0A30] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_EredrimOnly_ExamineKneel_Rise.GA_EredrimOnly_ExamineKneel_Rise_C [n: 4897F8] [c: 00007FF46EEC3D38] [or: 000001F634EDD530] [sps: 000001F615242DD0]
[000001F8D73E0310] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_EredrimOnly_Kneel_Rise.GA_EredrimOnly_Kneel_Rise_C [n: 4A820D] [c: 00007FF46EEC3D38] [or: 000001F634EDD490] [sps: 000001F615242DD0]
[000001F8D73EE710] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Generic_TakeFromNPC.GA_Generic_TakeFromNPC_C [n: 121592] [c: 00007FF46EEC3D38] [or: 000001F634EDD2B0] [sps: 000001F615242DD0]
[000001F8D73EDFF0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Gragu_TavernSitLoop.GA_Gragu_TavernSitLoop_C [n: 15DB2E] [c: 00007FF46EEC3D38] [or: 000001F634EDD260] [sps: 000001F615242DD0]
[000001F8D73ED1B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_BossSiphon_Interaction.GA_Player_BossSiphon_Interaction_C [n: 429268] [c: 00007FF46EEC3D38] [or: 000001F634EDCF90] [sps: 000001F615242DD0]
[000001F8D73ECA90] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_ChaliceInteract.GA_Player_ChaliceInteract_C [n: 4A821C] [c: 00007FF46EEC3D38] [or: 000001F634EDCF40] [sps: 000001F615242DD0]
[000001F8D73EC370] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_Beckon.GA_Player_DialogueEmote_Beckon_C [n: 4B0C29] [c: 00007FF46EEC3D38] [or: 000001F634EDCEF0] [sps: 000001F615242DD0]
[000001F8C61F0310] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_Disagree.GA_Player_DialogueEmote_Disagree_C [n: 25D632] [c: 00007FF46EEC3D38] [or: 000001F634EDE160] [sps: 000001F615242DD0]
[000001F8D73EFC70] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_LookAround.GA_Player_DialogueEmote_LookAround_C [n: 46240D] [c: 00007FF46EEC3D38] [or: 000001F634EDE110] [sps: 000001F615242DD0]
[000001F8D73EF8E0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DialogueEmote_Proclaim.GA_Player_DialogueEmote_Proclaim_C [n: 3DC886] [c: 00007FF46EEC3D38] [or: 000001F634EDDF30] [sps: 000001F615242DD0]
[000001F8D73EAA80] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_DrinkWhileSitting.GA_Player_DrinkWhileSitting_C [n: 213A96] [c: 00007FF46EEC3D38] [or: 000001F634EDDE40] [sps: 000001F615242DD0]
[000001F8D73EF550] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_FlyBack_End_Sariel.GA_Player_FlyBack_End_Sariel_C [n: 13FA24] [c: 00007FF46EEC3D38] [or: 000001F634EDDDF0] [sps: 000001F615242DD0]
[000001F8D73E9FD0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_FlyBack_Sariel.GA_Player_FlyBack_Sariel_C [n: 1D64F4] [c: 00007FF46EEC3D38] [or: 000001F634EDDC10] [sps: 000001F615242DD0]
[000001F8D73E98B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_GetUp_FromLayingOnBack.GA_Player_GetUp_FromLayingOnBack_C [n: 331F5D] [c: 00007FF46EEC3D38] [or: 000001F634EDDAD0] [sps: 000001F615242DD0]
[000001F8D73E9190] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_IntenseRemarkSlow.GA_Player_IntenseRemarkSlow_C [n: CEAFD] [c: 00007FF46EEC3D38] [or: 000001F634EDDA80] [sps: 000001F615242DD0]
[000001F8D73E8A70] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_Kneel_Loop.GA_Player_Kneel_Loop_C [n: 436D72] [c: 00007FF46EEC3D38] [or: 000001F634EDDA30] [sps: 000001F615242DD0]
[000001F8D73E8350] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_KneelLoop_Slower_YoungLazlo.GA_Player_KneelLoop_Slower_YoungLazlo_C [n: 2F93CD] [c: 00007FF46EEC3D38] [or: 000001F634EDD940] [sps: 000001F615242DD0]
[000001F8D73E7C30] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_KneelLoop_SlowerSmert.GA_Player_KneelLoop_SlowerSmert_C [n: 129F47] [c: 00007FF46EEC3D38] [or: 000001F634EDD8A0] [sps: 000001F615242DD0]
[000001F8D7517510] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_KneelRise_SlowerEredrim.GA_Player_KneelRise_SlowerEredrim_C [n: B4FD0] [c: 00007FF46EEC3D38] [or: 000001F634EDEB10] [sps: 000001F615242DD0]
[000001F8D7516DF0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_KneelRise_SlowerSmert.GA_Player_KneelRise_SlowerSmert_C [n: 269F92] [c: 00007FF46EEC3D38] [or: 000001F634EDEAC0] [sps: 000001F615242DD0]
[000001F8D75166D0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_Lazlo_Strike.GA_Player_Lazlo_Strike_C [n: 4B4CF5] [c: 00007FF46EEC3D38] [or: 000001F634EDE700] [sps: 000001F615242DD0]
[000001F8D7515FB0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_Prologue_Arise.GA_Player_Prologue_Arise_C [n: 45DB96] [c: 00007FF46EEC3D38] [or: 000001F634EDE5C0] [sps: 000001F615242DD0]
[000001F8D7515500] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_Prologue_Getup_After_Explosion.GA_Player_Prologue_Getup_After_Explosion_C [n: 77625] [c: 00007FF46EEC3D38] [or: 000001F634EDF0B0] [sps: 000001F615242DD0]
[000001F8D7515890] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_RiseFromKneel.GA_Player_RiseFromKneel_C [n: 4594EB] [c: 00007FF46EEC3D38] [or: 000001F634EDFF60] [sps: 000001F615242DD0]
[000001F8D7514A50] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_SarielTakeScythe_Memory.GA_Player_SarielTakeScythe_Memory_C [n: 1ECA8B] [c: 00007FF46EEC3D38] [or: 000001F634EDFBF0] [sps: 000001F615242DD0]
[000001F8D7514330] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_SitDown.GA_Player_SitDown_C [n: 25444F] [c: 00007FF46EEC3D38] [or: 000001F634EDFE20] [sps: 000001F615242DD0]
[000001F8D7513C10] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_SitThenLoop_WithCancel.GA_Player_SitThenLoop_WithCancel_C [n: F19B2] [c: 00007FF46EEC3D38] [or: 000001F634EDFA60] [sps: 000001F4DC018E00]
[000001F8D7513160] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_StandUp.GA_Player_StandUp_C [n: CAA4C] [c: 00007FF46EEC3D38] [or: 000001F8D7600740] [sps: 000001F615242DD0]
[000001F8D7512A40] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_StepBack_Longer.GA_Player_StepBack_Longer_C [n: 9A80D] [c: 00007FF46EEC3D38] [or: 000001F8D76006F0] [sps: 000001F615242DD0]
[000001F8D7512320] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Player_StepOutofShell.GA_Player_StepOutofShell_C [n: 259030] [c: 00007FF46EEC3D38] [or: 000001F8D7600650] [sps: 000001F615242DD0]
[000001F8D73EF1C0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Proxima_OnGround_Arrival.GA_Proxima_OnGround_Arrival_C [n: 1A2AFE] [c: 00007FF46EEC3D38] [or: 000001F8D7600560] [sps: 000001F615242DD0]
[000001F8D7511870] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Proxima_Standup.GA_Proxima_Standup_C [n: 295942] [c: 00007FF46EEC3D38] [or: 000001F8D76002E0] [sps: 000001F615242DD0]
[000001F8D7511150] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Sariel_InjuredSitUp.GA_Sariel_InjuredSitUp_C [n: 28D2EC] [c: 00007FF46EEC3D38] [or: 000001F8D76001F0] [sps: 000001F615242DD0]
[000001F8D73EEE30] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Sariel_LayOnGroundWeakly.GA_Sariel_LayOnGroundWeakly_C [n: 32D4DD] [c: 00007FF46EEC3D38] [or: 000001F8D76001A0] [sps: 000001F615242DD0]
[000001F8D751EAA0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/GA_Tiel_StartKneel.GA_Tiel_StartKneel_C [n: FE390] [c: 00007FF46EEC3D38] [or: 000001F8D7601460] [sps: 000001F615242DD0]
[000001F8D751E380] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Eredrim_Falls_PeaceAtLast.GA_Eredrim_Falls_PeaceAtLast_C [n: 4BD9F9] [c: 00007FF46EEC3D38] [or: 000001F8D76010F0] [sps: 000001F615242DD0]
[000001F8D751DC60] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Eredrim_GroundPose.GA_Eredrim_GroundPose_C [n: 22D03C] [c: 00007FF46EEC3D38] [or: 000001F8D7601050] [sps: 000001F615242DD0]
[000001F8D751FC70] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Pained_GroundLoop.GA_Pained_GroundLoop_C [n: 365EAA] [c: 00007FF46EEC3D38] [or: 000001F8D7600EC0] [sps: 000001F615242DD0]
[000001F8D751D1B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Pained_GroundPose.GA_Pained_GroundPose_C [n: 2F05A6] [c: 00007FF46EEC3D38] [or: 000001F8D7600E70] [sps: 000001F615242DD0]
[000001F8D751CA90] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Player_ProximaBiosamplerExamine.GA_Player_ProximaBiosamplerExamine_C [n: 4669D7] [c: 00007FF46EEC3D38] [or: 000001F8D7600C90] [sps: 000001F615242DD0]
[000001F8D751C370] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Interactions/Memories/GA_Sariel_CutDarkOvum.GA_Sariel_CutDarkOvum_C [n: 3CB86A] [c: 00007FF46EEC3D38] [or: 000001F8D7601B40] [sps: 000001F615242DD0]
```


---

## 15. Consolidated hook list for the sync layer

Ordered by what gives the most co-op fidelity per line of code. Every item names the exact class/member
documented above.

| # | Hook | Exact member | Why |
|---|---|---|---|
| 1 | ability activation | `USpartaGameplayAbility` (`/Script/Sparta.SpartaGameplayAbility`) + `FSpartaAbilitySet_GameplayAbility{Ability, AbilityLevel, InputTag}` (part 1 section 1.2) | one event per ability start/stop reproduces every action |
| 2 | the montage being played | `GA_PlayMontageBase_C:Montage` `0x5E0`, `:MontageStartSection` `0x600`, `:MontagePlayRate` `0x5E8`, `:SelectedAbilityMontage` `0x608` (part 1 section 5.1) | drives all animation and every hit window |
| 3 | attribute replication | `USpartaHealthSet` (36 attrs), `USpartaCombatSet` (50), `UPlayerAttributeSet` (32), `UShellAbilityAttributeSet` (32), `USidearmAttributeSet` (8), `UDarkPowerAttributeSet` (4) (part 1 section 2) | health/resolve/poise, ammo, exp, revive meter, heal charges |
| 4 | attribute write primitive | `USpartaAbilitySystemComponent::SetFloatAttributeValue(Attribute: GameplayAttribute, NewValue: float) -> bool` (part 1 section 1.3) | single call to apply a remote value |
| 5 | attribute watch primitive | `UAbilityTask_SpartaWaitAttChange::SpartaWaitForAttributeChange(...)` (part 1 section 4.1) | local change notification without polling |
| 6 | GE stack / status effects | `OnStackChanged(FActiveGameplayEffectHandle, NewCount, OldCount)`, `OnStatusEffectAdded/Removed(USpartaStatusEffect*)`, `Add/RemoveUnblockableInputTag(s)` (part 1 section 1.3) | status effects are the second half of the state |
| 7 | delayed GE countdowns | `PauseGameplayEffectDurationCountdowns(GameplayEffectQuery) -> TArray<FActiveGameplayEffectHandle>` / `ResumeGameplayEffectDurationCountdowns(TArray<...>)` | freeze a buff while re-syncing |
| 8 | cooldowns | `USpartaAbilitySubsystem` (5 maps + 20 functions, part 1 section 1.2) | global/unique cooldown state; note it is a GameInstanceSubsystem (per client) |
| 9 | input mirroring | `USpartaInputComponent::OnInputQueued(FGameplayTag InputTag, bool bPressed)` + `USpartaInputConfig.AbilityInputActions` (part 1 section 3) | replicate the tag, let each client resolve its own ability |
| 10 | two-actor abilities | `GA_Parry_Riposte_C` (`Victim`, `ParticipantSelectedMontage`), `GA_Traversal_ShellThrow_C` (`ShellCharacter`, `PlayerAnimation`, `ShellAnimation`) (part 1 section 7.5, part 2 section 14.1) | paired animations must run on both actors |
| 11 | shell/revive state | `GA_ShellReviveAction_C:ShellReviveCap 0x54C / ShellReviveIndex 0x558 / CanRevive 0x548` + `UPlayerAttributeSet:ShellReviveCurrentProgress` (part 2 section 10.2) | the revive meter is the only revive state |
| 12 | hit windows | `USpartaAnimNotifyState_HitCheck` / `USpartaAnimNotify_HitCheck` + `USpartaHitTrace` (part 1 section 4.3/4.4) | damage moments; deterministic from montage playback |
| 13 | mountage interruption | `USpartaMontageInterrupterComponent:OnMontageInterrupted(Montage, InterruptTag, Cause)` + `USpartaMontageInterrupterNotifyState` (part 1 section 4.2) | tells you *when* an animation may be cut, so a remote cancel is valid |
| 14 | ability grants | `USpartaAbilitySystemComponent::K2_GiveAbilityWithInputTag(AbilityClass, Level, InputTag)` / `CharacterPersistantAbilitySetData::GiveAbilitySet(Character)` (part 1 section 1.2-1.3) | transformations/shells change the grant set at runtime |
| 15 | weapon switch mid-combo | `GA_Player_Attack_Light_Axatana_C:Type 0x9A0`, `:TransformationMontage 0x9A8` (part 1 section 6.2) | the only ability that changes the equipped weapon while attacking |

Things a sync layer does **not** need to touch (documented here so nobody wastes time): the
`GA_Action_*` input handlers (part 1 section 8 -- they only translate input into the real action),
`GA_UpgradeBase`/`GA_ShellUpgrade_Base`/`GA_Artifact_*` passives (part 2 sections 11.2-11.3 -- they are
item-driven state), and the 193 interaction abilities (section 13 -- local/cinematic).

---

## 16. UNKNOWN list and what I tried

| # | Unknown | What I tried |
|---|---|---|
| 1 | **`FGameplayTag` strings** used for ability input | searched the whole index for a `GameplayTag` object type (`SELECT type,COUNT(*) WHERE type LIKE '%GameplayTag%'`), for `path LIKE '%InputTag%'` (72 rows, all property names), and for `GameplayTag` assets under `/Game` (none). The dump carries tag-typed *properties* but never tag *values*. |
| 2 | which attribute set is granted to the player pawn | `parse_dump.py show` on `CharacterPersistantAbilitySetData` / `SpartaAbilityDataAsset` (only an `FSpartaAbilitySet` field, no list), and a scan of every `Class` row for an attribute-set owner. Grant lists are Blueprint default values, not in the dump. |
| 3 | the montage each ability plays (the `Montage` default value) | the object dump has no CDO default-value records; I only list montage **assets whose path matches the ability name** (part 1 section 6.3) and mark those as name matches, not proof. |
| 4 | ability `Cost` / `CooldownDuration` **numbers** per ability | those are `float` properties on `USpartaGameplayAbility` (part 1 section 1.1); their values are Blueprint defaults, not dumped. Only the *existence* and offset are provable. |
| 5 | the five `USpartaInputInteraction` delegate signatures | `parse_dump.py find` for `FBindInputWithCallbacks*` / `SpartaInputInteractionOn*` returns 0 objects; the `DelegateProperty` rows have no resolvable signature struct. Header repeats `SpartaInputInteractionEvent(USpartaInputInteraction* Input)`. |
| 6 | element types of `SpartaAbilitySystemComponent:GetActiveStatusEffects` / `GetActiveAbilities` return arrays | the `ArrayProperty` rows have no resolvable `ai:` child; the map/array inner rows for these two are absent from the dump. |
| 7 | how "shell swap" is implemented | no `GA_*` class with Swap/ShellChange/TransferShell in its path (0 rows). Nearest provable facts: `/Script/Sparta.ItemFragment_SetShellClass`, `CharacterPersistantAbilitySetData::GiveAbilitySet/RemoveAbilitySet`. |
| 8 | whether the shell abilities read `UShellAbilityAttributeSet` | no ability property points at the set, and the set has no functions; the link is inside Blueprint graphs. Marked GUESS in section 10. |
| 9 | the `ESpartaAbility*` / `ESpartaMontageInterrupt*` enumerator values from the **dump** | the dump only proves the enum *type* of each property (`[em: ...]`); the enumerator names/values are quoted from `raw\CXXHeaderDump\Sparta_enums.hpp` and marked HDR. |
| 10 | `GA_Harden_Original` vs the "harden" ability named in the UI | only `GA_Harden_Original_C` (path `/Game/Sparta/Core/Characters/Player/Common/Abilities/StoneForm/`) exists; there is no `GA_Harden` class. Name match by folder (`StoneForm`) + `bInStoneForm`/`GE_Harden`/`BP_Harden_PP` members. |

