SELECT o.type, o.path, o.off, r.path AS refd FROM obj o JOIN obj p ON o.owner=p.addr
LEFT JOIN obj r ON r.addr=UPPER(o.ref)
WHERE p.path='/Game/Sparta/Core/Common/Interactions/Blueprints/BP_WoodenSelfClosingDoor.BP_WoodenSelfClosingDoor_C'
  AND o.off=880
