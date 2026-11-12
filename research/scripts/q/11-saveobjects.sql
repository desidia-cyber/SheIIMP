SELECT c.path, s.path AS parent FROM cls c LEFT JOIN obj s ON s.addr=UPPER(c.super)
WHERE c.path LIKE '/Game/Sparta/SaveObjects/%' ORDER BY c.path
