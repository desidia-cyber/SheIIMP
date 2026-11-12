-- All boss-side classes under the boss folder, BPs only
SELECT c.type || ' | ' || c.path || ' | ' || COALESCE(s.path,'?') AS row
FROM cls c LEFT JOIN obj s ON s.addr = UPPER(c.super)
WHERE c.path LIKE '/Game/Sparta/Characters/Bosses/%'
  AND c.type IN ('BlueprintGeneratedClass','WidgetBlueprintGeneratedClass')
  AND c.path NOT LIKE '%/GA\_%' ESCAPE '\'
ORDER BY c.path
