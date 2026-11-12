SELECT o.type, o.path
FROM obj o
WHERE o.path LIKE '/Game/Sparta/%'
  AND o.type IN ('LevelSequence','ActorSequence','CameraAnimationSequence')
ORDER BY o.type, o.path
