SELECT substr(c.path, length(rtrim(c.path, replace(c.path,'/','')))+1) AS name,
       substr(o.path, instr(o.path,':')+1) AS prop, o.type, printf('0x%X', o.off) AS off, r.path AS refd
FROM cls c JOIN obj o ON o.owner=c.addr LEFT JOIN obj r ON r.addr=UPPER(o.ref)
WHERE o.type NOT IN ('Function','DelegateFunction') AND o.path NOT LIKE '%UberGraphFrame%'
 AND c.path IN (
 '/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmActivationBase.GA_SidearmActivationBase_C',
 '/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmRangedAttackBase.GA_SidearmRangedAttackBase_C',
 '/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmRangedBurstAttackBase.GA_SidearmRangedBurstAttackBase_C',
 '/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmRangedChargedAttackBase.GA_SidearmRangedChargedAttackBase_C',
 '/Game/Sparta/Core/Player/Ability/Sidearm/GA_SidearmSecondaryActivation.GA_SidearmSecondaryActivation_C',
 '/Game/Sparta/Core/Player/Upgrades/Core/GA_UpgradeBase.GA_UpgradeBase_C',
 '/Game/Sparta/Core/Player/Upgrades/Shells/GA_ShellUpgrade_Base.GA_ShellUpgrade_Base_C',
 '/Game/Sparta/Core/Player/Upgrades/Core/GA_ShellAbility_InflictEffect.GA_ShellAbility_InflictEffect_C',
 '/Game/Sparta/Core/Player/Ability/ShellRevive/GA_ShellReviveBase.GA_ShellReviveBase_C',
 '/Game/Sparta/Core/Player/Ability/ShellRevive/GA_ShellReviveAction.GA_ShellReviveAction_C',
 '/Game/Sparta/Core/Player/Ability/ShellDowned/GA_ShellDownedBase.GA_ShellDownedBase_C',
 '/Game/Sparta/Core/Player/Ability/ShellSevered/GA_ShellSeveredBase.GA_ShellSeveredBase_C',
 '/Game/Sparta/Core/Player/Ability/ShellTraversal/GA_ShellTraversalBase.GA_ShellTraversalBase_C')
ORDER BY c.path, o.line
