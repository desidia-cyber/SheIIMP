-- which object types exist at all, to confirm the type names for BT/BB/ST assets
SELECT o.type || '   x' || COUNT(*)
FROM obj o
WHERE o.type LIKE '%Behavior%' OR o.type LIKE '%StateTree%' OR o.type LIKE '%Blackboard%'
GROUP BY o.type ORDER BY 2 DESC
