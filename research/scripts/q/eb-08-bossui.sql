-- Boss health bar / boss message widgets anywhere in /Game
SELECT c.type || ' | ' || c.path || ' | ' || COALESCE(s.path,'?')
FROM cls c LEFT JOIN obj s ON s.addr = UPPER(c.super)
WHERE c.path LIKE '%Boss%'
  AND ( c.type = 'WidgetBlueprintGeneratedClass' OR c.path LIKE '%WBP_%' )
ORDER BY c.path
