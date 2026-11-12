-- The currency grant/spend plumbing: BPFL_Player currency helpers, WBP_Player_Currency reads,
-- and the interactable that pays out.
WITH target(addr, path) AS (
  SELECT addr, path FROM cls WHERE path IN (
    '/Game/Sparta/Core/Player/BPFL_Player.BPFL_Player_C',
    '/Game/Sparta/UI/Player/WBP_Player_Currency.WBP_Player_Currency_C',
    '/Game/Sparta/Core/Player/BP_PlayerController.BP_PlayerController_C'
  )
)
SELECT t.path AS cls, substr(f.path, instr(f.path,':')+1) AS fn,
  COALESCE((SELECT group_concat(sig, ' , ') FROM (
      SELECT (substr(o.path, length(f.path)+2) || ': ' || o.type ||
              CASE WHEN r.path IS NOT NULL THEN ' of ' || r.path ELSE '' END) AS sig
      FROM obj o LEFT JOIN obj r ON r.addr=UPPER(o.ref)
      WHERE o.owner=f.addr ORDER BY o.line)),'') AS params
FROM target t JOIN obj f ON f.owner=t.addr AND f.type='Function'
WHERE (substr(f.path, instr(f.path,':')+1) LIKE '%Gold%'
    OR substr(f.path, instr(f.path,':')+1) LIKE '%Currency%'
    OR substr(f.path, instr(f.path,':')+1) LIKE 'S_Add%'
    OR substr(f.path, instr(f.path,':')+1) LIKE 'S_Remove%'
    OR substr(f.path, instr(f.path,':')+1) LIKE '%StackAmount%'
    OR substr(f.path, instr(f.path,':')+1) LIKE 'GetItemManager%')
ORDER BY t.path, fn
