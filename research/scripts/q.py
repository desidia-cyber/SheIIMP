#!/usr/bin/env python3
"""Run a SQL file against a dump index and print TSV.

  python q.py <db.sqlite3> <file.sql> [--limit N]

Table `obj`: addr, type, path, owner, super, ref, off, rest, line
  type   the object's class name as printed by the dumper ("Class", "Function",
         "ObjectProperty", "AnimSequence", "BP_Door_C", ...)
  path   full object path, hierarchical with ':' (class:member:parameter)
  owner  address of the Outer (owning package / class / function)
  super  SuperStruct of a class/struct
  ref    StructProperty's struct / ObjectProperty's class / ArrayProperty's inner type
  off    member offset as printed ([o: xx])
  rest   the raw bracket field of the original dump line
  line   line number in the dump file

Handy helpers live in the SQL itself; a `p()`-style python function is not available,
so write plain SQL.  Example files: research/scripts/q/*.sql
"""
import sqlite3
import sys


def main():
    db, sqlfile = sys.argv[1], sys.argv[2]
    limit = 0
    if '--limit' in sys.argv:
        limit = int(sys.argv[sys.argv.index('--limit') + 1])
    with open(sqlfile, 'r', encoding='utf-8') as f:
        sql = f.read()
    con = sqlite3.connect(db)
    cur = con.cursor()
    for stmt in [s.strip() for s in sql.split(';;') if s.strip()]:
        cur.execute(stmt)
        if cur.description:
            cols = [d[0] for d in cur.description]
            print('\t'.join(cols))
            n = 0
            for row in cur:
                print('\t'.join('' if v is None else str(v) for v in row))
                n += 1
                if limit and n >= limit:
                    print(f'# ... truncated at {limit} rows')
                    break
            print(f'# {n} rows')
        else:
            print(f'# ok ({stmt.split()[0]})')


if __name__ == '__main__':
    main()
