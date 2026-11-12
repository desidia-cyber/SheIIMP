SELECT substr(c.path, length(rtrim(c.path, replace(c.path,'/','')))+1) AS name,
       substr(o.path, instr(o.path,':')+1) AS prop, o.type, r.path AS refd
FROM cls c JOIN obj o ON o.owner=c.addr LEFT JOIN obj r ON r.addr=UPPER(o.ref)
WHERE o.type NOT IN ('Function','DelegateFunction') AND o.path NOT LIKE '%UberGraphFrame%'
 AND c.path IN (
 '/Game/Sparta/Core/Characters/Player/Common/Abilities/ActiveBlock/GA_ActiveBlock.GA_ActiveBlock_C',
 '/Game/Sparta/Core/Characters/Player/Common/Abilities/StoneForm/GA_Harden_Original.GA_Harden_Original_C',
 '/Game/Sparta/Core/Player/Ability/Parry/GA_Parry_Handler.GA_Parry_Handler_C',
 '/Game/Sparta/Core/Player/Ability/Parry/GA_Parry_Action_Single.GA_Parry_Action_Single_C',
 '/Game/Sparta/Core/Player/Ability/Parry/GA_Parry_Successful.GA_Parry_Successful_C',
 '/Game/Sparta/Core/Player/Ability/Parry/GA_Parry_Riposte.GA_Parry_Riposte_C',
 '/Game/Sparta/Core/Player/Ability/ShellRevive/GA_ShellReviveBase.GA_ShellReviveBase_C',
 '/Game/Sparta/Core/Player/Ability/ShellRevive/GA_ShellReviveAction.GA_ShellReviveAction_C',
 '/Game/Sparta/Core/Player/Ability/ShellRevive/GA_ShellRevive_SavedByTheShell.GA_ShellRevive_SavedByTheShell_C',
 '/Game/Sparta/Core/Player/Ability/ShellDowned/GA_ShellDownedBase.GA_ShellDownedBase_C',
 '/Game/Sparta/Core/Player/Ability/ShellSevered/GA_ShellSeveredBase.GA_ShellSeveredBase_C',
 '/Game/Sparta/Core/Player/Ability/ShellTraversal/GA_ShellTraversalBase.GA_ShellTraversalBase_C')
ORDER BY c.path, o.line
