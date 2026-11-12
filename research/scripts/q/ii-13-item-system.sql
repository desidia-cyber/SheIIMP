-- same compact dump for the item-definition / fragment system + shops / tarforge / drops
WITH target(addr, path) AS (
  SELECT addr, path FROM cls WHERE path IN (
    '/Script/Sparta.SpartaItemDefinition',
    '/Script/Sparta.SpartaItemInstance',
    '/Script/Sparta.SpartaItemFragment',
    '/Script/Sparta.SpartaItemFragment_StatLevels',
    '/Script/Sparta.ItemFragment_AbilitySets',
    '/Script/Sparta.ItemFragment_BlockAddAbility',
    '/Script/Sparta.ItemFragment_ClearAbilities',
    '/Script/Sparta.ItemFragment_Display',
    '/Script/Sparta.ItemFragment_OverrideDisplay',
    '/Script/Sparta.ItemFragment_Executable',
    '/Script/Sparta.ItemFragment_GiveItemToPlayer',
    '/Script/Sparta.ItemFragment_GiveItemToShell',
    '/Script/Sparta.ItemFragment_ItemTag',
    '/Script/Sparta.ItemFragment_PickupIcon',
    '/Script/Sparta.ItemFragment_SetDarkformClass',
    '/Script/Sparta.ItemFragment_SetShellClass',
    '/Script/Sparta.ItemFragment_SetStats',
    '/Script/Sparta.ItemFragment_Spawnable',
    '/Script/Sparta.ItemFragment_StackabilityRules',
    '/Script/Sparta.ItemFragment_StackableDivideSumByCountSquared',
    '/Script/Sparta.ItemFragment_Tarstone',
    '/Script/Sparta.ItemFragment_WorldStateModifier',
    '/Script/Sparta.PPItemFragment',
    '/Script/Sparta.PPItemFragmentData',
    '/Script/Sparta.ItemFragmentData_AbilitySets',
    '/Script/Sparta.ItemFragmentData_ClearAbilities',
    '/Script/Sparta.ItemFragmentData_TarstoneExperience',
    '/Script/Sparta.ItemDisplayTranslation',
    '/Script/Sparta.SpartaItemFunctionLibrary',
    '/Script/Sparta.Async_LoadSpartaItems',
    '/Script/Sparta.SpartaItemChangeEvent',
    '/Script/Sparta.PickupableActor',
    '/Script/Sparta.UpgradeGraph',
    '/Script/Sparta.UpgradeGraphNode',
    '/Script/Sparta.UpgradeGraphEdge',
    '/Script/Sparta.SpartaWeaponProgressionData'
  )
)
SELECT t.path AS cls, 'FUN' AS kind,
  substr(f.path, instr(f.path,':')+1) AS name,
  (SELECT group_concat(sig, ' , ') FROM (
      SELECT (substr(o.path, length(f.path)+2) || ': ' || o.type ||
              CASE WHEN r.path IS NOT NULL THEN ' of ' || r.path ELSE '' END) AS sig
      FROM obj o LEFT JOIN obj r ON r.addr=UPPER(o.ref)
      WHERE o.owner=f.addr ORDER BY o.line)) AS detail,
  NULL AS off
FROM target t JOIN obj f ON f.owner=t.addr AND f.type='Function'
UNION ALL
SELECT t.path, o.type, substr(o.path, instr(o.path,':')+1),
       COALESCE((SELECT r.path FROM obj r WHERE r.addr=UPPER(o.ref)),''), o.off
FROM target t JOIN obj o ON o.owner=t.addr AND o.type<>'Function'
ORDER BY cls, kind DESC, off
