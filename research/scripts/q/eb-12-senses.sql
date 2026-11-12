-- Perception sense configs declared on every AI-controller CDO (evidence for perception setup)
SELECT o.path
FROM obj o
WHERE o.path LIKE '%Controller.Default__%:PerceptionComponent.%'
ORDER BY o.path
