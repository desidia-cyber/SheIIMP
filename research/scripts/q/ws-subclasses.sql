SELECT p.path AS base, c.path AS derived, c.type
FROM obj p JOIN cls c ON UPPER(c.super) = p.addr
WHERE p.path IN ('/Script/Sparta.SpartaWorldObject','/Script/Sparta.SpartaInstancedObject','/Script/Sparta.SpartaSingleton')
ORDER BY p.path, c.path
