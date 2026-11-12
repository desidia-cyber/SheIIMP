SELECT substr(c.path, length('/Script/Sparta.')) AS cls, substr(o.path, length(c.path)+2) AS prop,
       o.type AS ptype, printf('0x%x', o.off) AS offpath, r.path AS refd
FROM obj c JOIN obj o ON o.owner = c.addr
LEFT JOIN obj r ON r.addr = UPPER(o.ref)
WHERE c.path IN (
 '/Script/Sparta.SpartaSaveObject','/Script/Sparta.SpartaSaveComponent','/Script/Sparta.SpartaSaveSubSystem',
 '/Script/Sparta.SpartaSaveGame','/Script/Sparta.SpartaMetaSave','/Script/Sparta.SpartaSaveInterface',
 '/Script/Sparta.SpartaWorldObject','/Script/Sparta.SpartaInstancedObject','/Script/Sparta.SpartaSingleton',
 '/Script/Sparta.DataLayersSave','/Script/Sparta.PossibleWorldStateSet','/Script/Sparta.SpartaSessionSaveObjectBase',
 '/Script/Sparta.PlayerSaveObject','/Script/Sparta.SpartaSaveGlobalSettings','/Script/Sparta.SpartaDataLayerSubsystem',
 '/Script/Sparta.TarstoneSaveData','/Script/Sparta.PPItemsSaveData','/Script/Sparta.PlayerProgressionSaveData',
 '/Script/Sparta.ItemFragment_WorldStateModifier')
  AND o.type NOT IN ('Function','DelegateFunction')
ORDER BY c.path, o.off, o.line
