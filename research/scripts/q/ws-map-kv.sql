SELECT substr(o.path,1,0) AS d, o.type AS kv_type, o.path AS kv_path, r.path AS refd
FROM obj o JOIN obj p ON p.addr = o.owner
LEFT JOIN obj r ON r.addr = UPPER(o.ref)
WHERE p.path IN ('/Script/Sparta.SpartaSaveGame:ActorFloats','/Script/Sparta.SpartaSaveGame:ActorInts',
                 '/Script/Sparta.SpartaSaveGame:ActorNames','/Script/Sparta.SpartaSaveGame:ActorBools',
                 '/Script/Sparta.SpartaSaveGame:ActorObjectMap','/Script/Sparta.SpartaSaveSubSystem:SaveObjects',
                 '/Script/Sparta.SpartaSaveGame:ChangedActors','/Script/Sparta.DataLayersSave:SavedDataLayerStates',
                 '/Script/Sparta.PossibleWorldStateSet:WorldStates')
ORDER BY p.path, o.line
