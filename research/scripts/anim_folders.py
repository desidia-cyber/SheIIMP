#!/usr/bin/env python3
"""Show the folder vocabulary of the animation assets, so the category rules in
anim_catalog.py can be written from evidence instead of guesswork.

Usage: python anim_folders.py <db.sqlite3> [--prefix /Game/Sparta/Characters/]
"""
import collections
import sqlite3
import sys


def main():
    db = sys.argv[1]
    prefix = '/Game/Sparta/Characters/'
    if '--prefix' in sys.argv:
        prefix = sys.argv[sys.argv.index('--prefix') + 1]
    con = sqlite3.connect(db)
    rows = con.execute(
        "SELECT path FROM obj WHERE type IN ('AnimSequence','AnimMontage') AND path LIKE ? AND path NOT LIKE '%:%'",
        (prefix + '%',)).fetchall()
    # folder the asset sits in (immediately above the file) and its parent folder
    direct = collections.Counter()
    chain = collections.Counter()
    for (p,) in rows:
        folder = p.rsplit('/', 1)[0]
        direct[folder] += 1
        rel = folder[len(prefix):] if folder.startswith(prefix) else folder
        parts = [x for x in rel.split('/') if x]
        # every folder name seen anywhere in the relative path
        for part in parts:
            chain[part] += 1
    print('# folder names anywhere under the prefix (segment -> number of assets)')
    for name, n in sorted(chain.items(), key=lambda kv: -kv[1]):
        print(f'{n:6d}  {name}')
    print()
    print('# the folder each asset sits directly in (top 80)')
    for name, n in sorted(direct.items(), key=lambda kv: -kv[1])[:80]:
        print(f'{n:6d}  {name}')


if __name__ == '__main__':
    main()
