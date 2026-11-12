-- every struct whose SuperStruct is Engine.TableRowBase => a DataTable row struct
SELECT o.type, o.path, s.path AS parent
FROM obj o JOIN obj s ON s.addr = UPPER(o.super)
WHERE s.path = '/Script/Engine.TableRowBase'
ORDER BY o.path
