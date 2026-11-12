-- BehaviourTree / BlackboardData / StateTree assets used by enemies and bosses
SELECT o.type || ' | ' || o.path
FROM obj o
WHERE o.type IN ('BehaviorTree','BlackboardData','StateTree','StateTreeBlueprint','BehaviorTreeBlueprint')
  AND ( o.path LIKE '/Game/Sparta/Characters/Enemies/%' OR o.path LIKE '/Game/Sparta/Characters/Bosses/%'
        OR o.path LIKE '/Game/Sparta/Core/AI/%' )
ORDER BY o.type, o.path
