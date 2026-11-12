-- All enemy-side classes under the enemy folder, BPs only (exclude anim BP, GA_ abilities, ABP, ABPL)
SELECT c.type || ' | ' || c.path || ' | ' || COALESCE(s.path,'?') AS row
FROM cls c LEFT JOIN obj s ON s.addr = UPPER(c.super)
WHERE c.path LIKE '/Game/Sparta/Characters/Enemies/%'
  AND c.type IN ('BlueprintGeneratedClass','WidgetBlueprintGeneratedClass')
  AND c.path NOT LIKE '%/GA\_%' ESCAPE '\'
ORDER BY c.path
