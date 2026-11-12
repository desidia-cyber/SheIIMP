SELECT substr(o.path, instr(o.path,':')+1) AS member, c.path AS klass, o.type, o.off, r.path AS refd
FROM obj c JOIN obj o ON o.owner = c.addr
LEFT JOIN obj r ON r.addr = UPPER(o.ref)
WHERE o.type NOT IN ('Function','DelegateFunction')
  AND ( c.path IN (
     '/Script/Sparta.SpartaAnimNotify_HitCheck','/Script/Sparta.SpartaAnimNotifyState_HitCheck',
     '/Script/Sparta.SpartaAnimNotify_CharacterEvent','/Script/Sparta.SpartaAnimNotify_WeaponEvent',
     '/Script/Sparta.AnimNotifyState_InputBlock','/Script/Sparta.AnimNotifyState_InputQueue',
     '/Script/Sparta.SpartaInputConfig','/Script/Sparta.SpartaInputComponent','/Script/Sparta.SpartaInputInteraction',
     '/Script/Sparta.SpartaInputModifierAllowInputWithTags','/Script/Sparta.SpartaInputModifierBlockInputTagQuery',
     '/Script/Sparta.SpartaInputModifierBlockInputWithTags','/Script/Sparta.SpartaInputModifierControllerStickOnlyXAxis',
     '/Script/Sparta.SpartaInputModifierQuantizeMagnitude','/Script/Sparta.SpartaInputModifierResponseCurveMagnitude',
     '/Script/Sparta.SpartaInputModifierScaleAction','/Script/Sparta.SpartaInputTriggerConditional',
     '/Script/Sparta.SpartaInputTriggerHoldThenPulse','/Script/Sparta.SpartaInputTriggerMouseAccumulatedThreshold',
     '/Script/Sparta.SpartaInputTriggerTimeout','/Script/Sparta.SpartaEnhancedPlayerInput',
     '/Script/Sparta.SpartaAbilityDataAsset','/Script/Sparta.CharacterPersistantAbilitySetData' ) )
ORDER BY c.path, o.line
