SELECT addr, type, path, super, off, line, substr(rest,1,120) AS rest
FROM obj WHERE addr='000001F97582FC70'
;;
SELECT addr, type, path FROM obj WHERE path LIKE 'BlueprintGenKey%' OR path LIKE '%Dungeon_EndSliceKey_02%'
