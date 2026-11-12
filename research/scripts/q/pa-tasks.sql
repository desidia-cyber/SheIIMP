SELECT d.path AS sig,
       (SELECT group_concat(substr(o.path, instr(o.path,':')+1)||' : '||o.type||' ['||printf('0x%X', o.off)||']', ', ')
          FROM obj o WHERE o.owner=d.addr ORDER BY o.line) AS params
FROM obj d
WHERE d.type='DelegateFunction' AND (d.path LIKE '%AbilityTask_%' OR d.path LIKE '%OnSurfacePointsFound%' OR d.path LIKE '%WaitAttChange%')
ORDER BY d.path;;
SELECT substr(o.path, instr(o.path,':')+1) AS member, o.type, o.off, r.path AS refd
FROM obj c JOIN obj o ON o.owner=c.addr LEFT JOIN obj r ON r.addr=UPPER(o.ref)
WHERE c.path='/Script/Sparta.SpartaHitTrace' AND o.type NOT IN ('Function','DelegateFunction')
ORDER BY o.line
