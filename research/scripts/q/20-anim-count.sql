SELECT type, COUNT(*) AS n FROM obj WHERE type IN ('AnimSequence','AnimMontage') AND path LIKE '/Game/Sparta/Characters/%' AND path NOT LIKE '%:%' GROUP BY type
