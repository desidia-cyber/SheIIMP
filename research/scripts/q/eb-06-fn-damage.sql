-- Functions of the whole damage path family
SELECT oc.path || ' :: ' || f.path || ' ('
  || COALESCE((SELECT GROUP_CONCAT(p.type || ' ' || p.path, ' ; ' ORDER BY p.line)
               FROM obj p WHERE p.owner = f.addr AND p.type LIKE '%Property'), '')
  || ')' AS sig
FROM obj f
JOIN obj oc ON oc.addr = f.owner
WHERE f.type = 'Function'
  AND ( oc.path LIKE '/Script/Sparta.SpartaHit%'
     OR oc.path LIKE '/Script/Sparta.SpartaDamage%'
     OR oc.path LIKE '/Script/Sparta.SpartaHittable'
     OR oc.path LIKE '/Script/Sparta.SpartaHealth%'
     OR oc.path LIKE '/Script/Sparta.SpartaPoise%'
     OR oc.path LIKE '/Script/Sparta.SpartaBreak%'
     OR oc.path LIKE '/Script/Sparta.SpartaHeal%'
     OR oc.path LIKE '/Script/Sparta.SpartaWinded%'
     OR oc.path LIKE '/Script/Sparta.SpartaWinded%'
     OR oc.path LIKE '/Script/Sparta.SpartaDeath%'
     OR oc.path LIKE '/Script/Sparta.SpartaExecution%'
     OR oc.path LIKE '/Script/Sparta.%ExecutionModifier'
     OR oc.path LIKE '/Script/Sparta.DamageExecutionModifier'
     OR oc.path LIKE '/Script/Sparta.HitPayloadModifier'
     OR oc.path LIKE '/Script/Sparta.BreakExecutionModifier'
     OR oc.path LIKE '/Script/Sparta.HealExecutionModifier'
     OR oc.path LIKE '/Script/Sparta.PoiseExecutionModifier'
     OR oc.path LIKE '/Script/Sparta.WindedExecutionModifier'
     OR oc.path LIKE '/Script/Sparta.WindedDamageBonus'
     OR oc.path LIKE '/Script/Sparta.WindedPoiseDamageBonus'
     OR oc.path LIKE '/Script/Sparta.SpartaAnimNotify%HitCheck'
     OR oc.path LIKE '/Script/Sparta.SpartaHitReactionAnimSet'
     OR oc.path LIKE '/Script/Sparta.SpartaPhysicalHitReactionData' )
ORDER BY oc.path, f.line
