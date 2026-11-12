-- how many Packages (=assets) under Items
SELECT COUNT(*) AS n FROM obj WHERE type='Package' AND path LIKE '/Game/Sparta/Items/%'
;;
-- item definition classes: parent is SpartaItemDefinition
SELECT c.path, s.path AS parent FROM cls c JOIN obj s ON s.addr=UPPER(c.super)
WHERE s.path='/Script/Sparta.SpartaItemDefinition' ORDER BY c.path
;;
-- subclasses of the fragment base
SELECT c.path, s.path AS parent FROM cls c JOIN obj s ON s.addr=UPPER(c.super)
WHERE s.path LIKE '/Script/Sparta.%ItemFragment%' ORDER BY c.path
