SELECT d.path AS sig,
       (SELECT group_concat(substr(o.path, instr(o.path,':')+1)||' : '||o.type||' ['||printf('0x%X', o.off)||']', ', ')
          FROM obj o WHERE o.owner=d.addr ORDER BY o.line) AS params
FROM obj d
WHERE d.type='DelegateFunction' AND d.path LIKE '/Script/Sparta.%'
  AND (d.path LIKE '%OnInputQueued%' OR d.path LIKE '%OnStackChanged%' OR d.path LIKE '%MontageInterrupted%'
       OR d.path LIKE '%StatusEffect%' OR d.path LIKE '%UpgradeStat%' OR d.path LIKE '%FailedToApply%'
       OR d.path LIKE '%InputStarted%' OR d.path LIKE '%InputOngoing%' OR d.path LIKE '%InputCompleted%'
       OR d.path LIKE '%InputCancelled%' OR d.path LIKE '%ProgressChanged%')
ORDER BY d.path
