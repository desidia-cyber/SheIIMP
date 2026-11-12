-- Byte-identical dump-line reconstruction (no space between path and rest)
SELECT '[' || addr || '] ' || type || ' ' || path || COALESCE(rest,'') AS line
FROM obj WHERE path IN (
  '/Game/Sparta/Characters/Enemies/Aristocrat/BP_Aristocrat.BP_Aristocrat_C',
  '/Game/Sparta/Characters/Bosses/Dwarf/BP_DwarfBoss.BP_DwarfBoss_C',
  '/Script/Sparta.SpartaHittable',
  '/Script/Sparta.SpartaHitPayload'
)
