SELECT type, path FROM obj WHERE path LIKE '/Game/Sparta/Core/Characters/Player/%Default__%:CharacterMesh0' ORDER BY path;;
SELECT type, path FROM obj WHERE path LIKE '/Game/Sparta/Core/Characters/Player/%Default__%' AND type='SpartaSkeletalMeshComponent' ORDER BY path
