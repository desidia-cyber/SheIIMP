SELECT c.type, c.path, s.path AS parent
FROM cls c LEFT JOIN obj s ON s.addr=UPPER(c.super)
WHERE c.type='WidgetBlueprintGeneratedClass'
  AND (c.path LIKE '/Game/Sparta/UI/%' OR c.path LIKE '/Game/Sparta/Widgets/%')
ORDER BY c.path
