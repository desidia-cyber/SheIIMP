-- Function signatures for the boss UI widgets and the boss-AI component
SELECT oc.path || ' :: ' || f.path || ' ('
  || COALESCE((SELECT GROUP_CONCAT(p.type || ' ' || p.path, ' ; ' ORDER BY p.line)
               FROM obj p WHERE p.owner = f.addr AND p.type LIKE '%Property'
                 AND p.path NOT LIKE '%:CallFunc\_%' ESCAPE '\'
                 AND p.path NOT LIKE '%:K2Node\_%' ESCAPE '\'
                 AND p.path NOT LIKE '%:Temp\_%' ESCAPE '\'
                 AND p.path NOT LIKE '%:Goto\_%' ESCAPE '\'
                 AND p.path NOT LIKE '%__Internal\_%' ESCAPE '\'), '')
  || ')'
FROM obj f JOIN obj oc ON oc.addr = f.owner
WHERE f.type='Function'
  AND ( oc.path LIKE '/Game/Sparta/UI/Enemy/WBP_Enemy_Boss%'
     OR oc.path LIKE '/Game/Sparta/UI/Menu/WBP_BossDefeatedMessage%'
     OR oc.path LIKE '/Game/Sparta/Core/AI/Components/BPC_BossAI%'
     OR oc.path LIKE '/Game/Sparta/Core/AI/Components/BPC_EnemyHealthWidget%'
     OR oc.path LIKE '/Game/Sparta/Core/AI/Components/BPC_AttackWarningInvoker%'
     OR oc.path LIKE '/Game/Sparta/Core/AI/Components/BPC_ShellSummonHealth%'
     OR oc.path LIKE '/Game/Sparta/Core/AI/Components/BPC_AstralAI%' )
ORDER BY oc.path, f.line
