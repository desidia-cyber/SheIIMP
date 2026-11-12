#!/usr/bin/env python3
"""Audit object paths quoted in the reports against the dump index.

The reports name hundreds of classes and assets in tables.  This tool extracts every `/Game/...`
and `/Script/...` token from a report and checks that the index knows that path (as an object path,
or as the prefix of one).  Anything the index does not know is printed so a human can decide whether
it is a legitimate shorthand (a folder, an elided path with `…`) or an invented path.

Usage: python audit_paths.py [files...]
"""
import glob
import os
import re
import sqlite3
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RESEARCH = os.path.dirname(HERE)
TOKEN = re.compile(r'(/(?:Game|Script|Engine)/[A-Za-z0-9_./:+\-]+)')


def clean(tok):
    tok = tok.rstrip('.,;:')
    # drop a trailing ".Object" duplicate that markdown sometimes keeps, and trailing '_'
    return tok


def main():
    files = sys.argv[1:] or sorted(glob.glob(os.path.join(RESEARCH, '*.md')))
    con = sqlite3.connect(os.path.join(RESEARCH, 'raw', 'index.sqlite3'))
    cache = {}

    def known(tok):
        if tok in cache:
            return cache[tok]
        row = con.execute('SELECT 1 FROM obj WHERE path=? LIMIT 1', (tok,)).fetchone()
        if row:
            cache[tok] = True
            return True
        row = con.execute('SELECT 1 FROM obj WHERE path LIKE ? LIMIT 1', (tok + '%',)).fetchone()
        cache[tok] = bool(row)
        return cache[tok]

    grand_total = grand_bad = 0
    for path in files:
        total = bad = 0
        misses = []
        with open(path, 'r', encoding='utf-8', errors='replace') as f:
            for line in f:
                for m in TOKEN.finditer(line):
                    tok = clean(m.group(1))
                    if tok.endswith('/'):
                        continue
                    total += 1
                    if not known(tok):
                        bad += 1
                        if len(misses) < 8 and tok not in misses:
                            misses.append(tok)
        grand_total += total
        grand_bad += bad
        print(f'{os.path.basename(path):38s} paths={total:6d} unknown={bad:5d}')
        for s in misses:
            print(f'    ? {s}')
    print(f'\ntotal paths={grand_total} unknown={grand_bad}')


if __name__ == '__main__':
    main()
