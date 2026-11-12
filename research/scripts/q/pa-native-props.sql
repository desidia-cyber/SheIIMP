-- compact property list for a set of native Sparta classes
SELECT c.path AS klass, substr(o.path, instr(o.path, ':')+1) AS prop, o.type, o.off, r.path AS refd
FROM obj c JOIN obj o ON o.owner = c.addr
LEFT JOIN obj r ON r.addr = UPPER(o.ref)
WHERE c.type='Class' AND c.path IN (
 '/Script/Sparta.SpartaGameplayAbility','/Script/Sparta.SpartaAttackAbility','/Script/Sparta.SpartaAbilityCost',
 '/Script/Sparta.SpartaAbilityDataAsset','/Script/Sparta.SpartaAbilityTagRelationshipMapping',
 '/Script/Sparta.SpartaAbilitySetHandleContainer','/Script/Sparta.CharacterPersistantAbilitySetData',
 '/Script/Sparta.SpartaAbilityActor','/Script/Sparta.SpartaAbilitySubsystem','/Script/Sparta.SpartaAbilitySystemComponent',
 '/Script/Sparta.SpartaMontageInterrupterComponent','/Script/Sparta.SpartaMontageInterrupterNotifyState')
AND o.type NOT IN ('Function','DelegateFunction')
ORDER BY c.path, o.line
