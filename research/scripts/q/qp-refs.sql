-- resolve ref types for named member rows
SELECT o.path, o.type, o.off, r.path AS refd
FROM obj o LEFT JOIN obj r ON r.addr=UPPER(o.ref)
WHERE (o.path LIKE 'SaveObjects./Script/Sparta.SpartaSaveSubSystem%'
    OR o.path LIKE '/Script/Sparta.SpartaSaveContext%'
    OR o.path LIKE '/Script/Sparta.SaveObjectTickFunction%'
    OR o.path LIKE '/Script/Sparta.ActiveDialogue%'
    OR o.path LIKE '/Script/Sparta.ShellSkillSet%'
    OR o.path LIKE '/Script/Sparta.SpartaZoneData%'
    OR o.path LIKE '/Script/Sparta.SpartaActivityType%')
ORDER BY o.path
