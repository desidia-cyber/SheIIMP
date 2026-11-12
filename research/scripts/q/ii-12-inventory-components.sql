-- Compact member dump for a list of exact class paths:
--   one row per Function with its full ordered parameter list,
--   one row per non-function member (property/delegate) with offset + resolved type.
WITH target(addr, path) AS (
  SELECT addr, path FROM cls WHERE path IN (
    '/Script/Sparta.PlayerInventoryComponent',
    '/Script/Sparta.PPItemsComponent',
    '/Script/Sparta.PPItemsSaveData',
    '/Script/Sparta.WeaponsComponent',
    '/Script/Sparta.UpgradesComponent',
    '/Script/Sparta.TarstoneComponent',
    '/Script/Sparta.TarstoneSaveData',
    '/Script/Sparta.ArtifactComponent',
    '/Script/Sparta.ArmorComponent',
    '/Script/Sparta.SlayerSealComponent',
    '/Script/Sparta.SidearmProjectileCacheComponent',
    '/Script/Sparta.SpartaItemManagerComponent'
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
SELECT t.path, o.type, substr(o.path, instr(o.path,':')+1), COALESCE(r.path,''), o.off
FROM target t JOIN obj o ON o.owner=t.addr AND o.type<>'Function'
LEFT JOIN obj r ON r.addr=UPPER(o.ref)
ORDER BY cls, kind DESC, off
