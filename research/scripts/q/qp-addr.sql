-- resolve addresses (as hex text) to object paths, for delegate-signature targets
SELECT addr, type, path FROM obj
WHERE addr IN ('00007FF46F3CEE80','00007FF46F3D24F0','00007FF46F3D1B28','00007FF46F2E3578')
