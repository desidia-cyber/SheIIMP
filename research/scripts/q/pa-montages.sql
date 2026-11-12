SELECT o.path FROM obj o WHERE o.type='AnimMontage' AND o.path LIKE '/Game/Sparta/%' AND o.path NOT LIKE '%Enemies%' AND o.path NOT LIKE '%Bosses%' ORDER BY o.path LIMIT 400
