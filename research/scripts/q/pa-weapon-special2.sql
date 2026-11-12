SELECT substr(c.path, length(rtrim(c.path, replace(c.path,'/','')))+1) AS name,
       substr(o.path, instr(o.path,':')+1) AS prop, o.type, printf('0x%X', o.off) AS off, r.path AS refd
FROM cls c JOIN obj o ON o.owner=c.addr LEFT JOIN obj r ON r.addr=UPPER(o.ref)
WHERE o.type NOT IN ('Function','DelegateFunction') AND o.path NOT LIKE '%UberGraphFrame%'
 AND c.path IN (
 '/Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Player_Attack_Heavy_Ataxana.GA_Player_Attack_Heavy_Ataxana_C',
 '/Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Player_Attack_Light_Axatana.GA_Player_Attack_Light_Axatana_C',
 '/Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Axatana_Handler.GA_Axatana_Handler_C',
 '/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_Light_Base.GA_Player_Attack_ClockworkScythe_Light_Base_C',
 '/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_Heavy_Base.GA_Player_Attack_ClockworkScythe_Heavy_Base_C')
ORDER BY c.path, o.line
