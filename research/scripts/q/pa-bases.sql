SELECT substr(c.path, length(rtrim(c.path, replace(c.path,'/','')))+1) AS name,
       substr(o.path, instr(o.path,':')+1) AS prop, o.type, o.off, r.path AS refd
FROM cls c JOIN obj o ON o.owner=c.addr LEFT JOIN obj r ON r.addr=UPPER(o.ref)
WHERE o.type NOT IN ('Function','DelegateFunction')
  AND c.path IN (
  '/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C',
  '/Game/Sparta/Core/Ability/GA_SpartaBase.GA_SpartaBase_C',
  '/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C',
  '/Game/Sparta/Core/Ability/GA_AttackBase.GA_AttackBase_C',
  '/Game/Sparta/Core/Ability/GA_AttackBase_Melee.GA_AttackBase_Melee_C',
  '/Game/Sparta/Core/Ability/GA_StrafeBase.GA_StrafeBase_C',
  '/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelectorBase.GA_Player_AttackSelectorBase_C',
  '/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C',
  '/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C',
  '/Game/Sparta/Core/Player/Ability/GA_CharacterAbilityBase.GA_CharacterAbilityBase_C',
  '/Game/Sparta/Core/Player/Ability/GA_ShellAbilityBase.GA_ShellAbilityBase_C',
  '/Game/Sparta/Core/Player/Ability/GA_WeaponAbilityBase.GA_WeaponAbilityBase_C',
  '/Game/Sparta/Core/Player/Ability/GA_WeaponBuffBase.GA_WeaponBuffBase_C',
  '/Game/Sparta/Core/Player/Ability/GA_SidearmBuffBase.GA_SidearmBuffBase_C',
  '/Game/Sparta/Core/Player/Ability/Charges/GA_ChargesBase.GA_ChargesBase_C',
  '/Game/Sparta/Core/Player/Ability/Dash/GA_DashBase.GA_DashBase_C',
  '/Game/Sparta/Core/Player/Ability/Roll/GA_RollBase.GA_RollBase_C')
ORDER BY c.path, o.line
