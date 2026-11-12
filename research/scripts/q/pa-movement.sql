SELECT substr(c.path, length(rtrim(c.path, replace(c.path,'/','')))+1) AS name,
       substr(o.path, instr(o.path,':')+1) AS prop, o.type, printf('0x%X', o.off) AS off, r.path AS refd
FROM cls c JOIN obj o ON o.owner=c.addr LEFT JOIN obj r ON r.addr=UPPER(o.ref)
WHERE o.type NOT IN ('Function','DelegateFunction') AND o.path NOT LIKE '%UberGraphFrame%'
 AND c.path IN (
 '/Game/Sparta/Core/Player/Ability/Plunging/GA_Player_Attack_Plunging.GA_Player_Attack_Plunging_C',
 '/Game/Sparta/Core/Player/Ability/Input/GA_Aiming_Handler.GA_Aiming_Handler_C',
 '/Game/Sparta/Core/Player/Ability/GA_Sprint.GA_Sprint_C',
 '/Game/Sparta/Core/Player/Ability/GA_Walk.GA_Walk_C',
 '/Game/Sparta/Core/Player/Ability/GA_Death_Player.GA_Death_Player_C',
 '/Game/Sparta/Core/Player/Ability/Charges/GA_Charges_Channeling.GA_Charges_Channeling_C',
 '/Game/Sparta/Core/Player/Ability/Charges/GA_ChargesHandler_Gragu.GA_ChargesHandler_Gragu_C',
 '/Game/Sparta/Core/Player/Ability/GA_ToggleLockOn.GA_ToggleLockOn_C',
 '/Game/Sparta/Core/Player/Ability/GA_PreventDeathHandler.GA_PreventDeathHandler_C',
 '/Game/Sparta/Core/Player/Ability/GA_Player_ExecutionBase.GA_Player_ExecutionBase_C',
 '/Game/Sparta/Core/Player/Ability/Dash/GA_Dash_Tiel.GA_Dash_Tiel_C')
ORDER BY c.path, o.line
