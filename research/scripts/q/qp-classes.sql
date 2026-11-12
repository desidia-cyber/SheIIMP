-- quests/progression/dialogue/save/map : every class-like object whose path matches my area
SELECT c.type, c.path, s.path AS parent
FROM cls c LEFT JOIN obj s ON s.addr=UPPER(c.super)
WHERE (c.path LIKE '%Quest%' OR c.path LIKE '%Objective%' OR c.path LIKE '%Story%'
    OR c.path LIKE '%Progress%' OR c.path LIKE '%Dialogue%' OR c.path LIKE '%Dialog%'
    OR c.path LIKE '%Conversation%' OR c.path LIKE '%Dlg%' OR c.path LIKE '%NPC%'
    OR c.path LIKE '%Merchant%' OR c.path LIKE '%Vendor%' OR c.path LIKE '%Journal%'
    OR c.path LIKE '%Lore%' OR c.path LIKE '%Tutorial%' OR c.path LIKE '%Unlock%'
    OR c.path LIKE '%Milestone%' OR c.path LIKE '%Ending%' OR c.path LIKE '%Credits%'
    OR c.path LIKE '%Achievement%' OR c.path LIKE '%Activity%')
ORDER BY c.path
