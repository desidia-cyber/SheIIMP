-- dialogue assets and other asset objects relevant to dialogue/narrative
SELECT o.type, o.path
FROM obj o
WHERE (o.type LIKE '%DlgDialogue%' OR o.path LIKE '/Game/%/DlgDialogue%')
ORDER BY o.path
;;
-- console-command-ish classes (Con_*/Cheat*/debug command helpers)
SELECT c.path, s.path AS parent
FROM cls c LEFT JOIN obj s ON s.addr=UPPER(c.super)
WHERE (c.path LIKE '%Con\_Check%' ESCAPE '\' OR c.path LIKE '%Cheat%' OR c.path LIKE '%ConsoleCommand%'
       OR c.path LIKE '%/Debug/%' OR c.path LIKE '%DebugMenu%')
ORDER BY c.path
