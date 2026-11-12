SELECT s.path AS base, c.path AS saveobject_class
FROM cls c JOIN obj s ON s.addr = UPPER(c.super)
WHERE s.path IN ('/Script/Sparta.SpartaSaveObject','/Script/Sparta.PlayerSaveObject','/Script/Sparta.PlayerProgressionSaveData',
                 '/Script/Sparta.SpartaSessionSaveObjectBase','/Script/Sparta.DataLayersSave','/Script/Sparta.PPItemsSaveData',
                 '/Script/Sparta.TarstoneSaveData','/Script/Sparta.SpartaInstancedObject','/Script/Sparta.SpartaWorldObject')
ORDER BY s.path, c.path
