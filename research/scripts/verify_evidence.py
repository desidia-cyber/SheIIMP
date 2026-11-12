#!/usr/bin/env python3
"""Quality gate for the research reports: every evidence line must exist in the dump.

Any line in a report that looks like a dump line

    [00000291B2C7FB00] StructProperty /Script/Sparta.PlayerAttributeSet:CurrentEXP [o: 30] ...

is looked up in the SQLite index by address; the script then checks that the object found at that
address has the same type and the same path.  Reports are supposed to quote dump lines verbatim, so
a mismatch means the line was transcribed or invented.

Usage:
  python verify_evidence.py [--db raw\index.sqlite3] [files...]
Default: every .md file in the research root.
"""
import glob
import os
import re
import sqlite3
import sys

LINE = re.compile(r'^\s*\[([0-9A-Fa-f]{8,16})\]\s+(\S+)\s+(\S+)')
HERE = os.path.dirname(os.path.abspath(__file__))
RESEARCH = os.path.dirname(HERE)


def main():
    db = os.path.join(RESEARCH, 'raw', 'index.sqlite3')
    args = [a for a in sys.argv[1:]]
    if '--db' in args:
        db = args[args.index('--db') + 1]
        del args[args.index('--db'):args.index('--db') + 2]
    files = args or sorted(glob.glob(os.path.join(RESEARCH, '*.md')))
    con = sqlite3.connect(db)
    total_bad = 0
    print(f'{"file":38s} {"lines":>7s} {"ok":>6s} {"no-addr":>8s} {"mismatch":>9s}')
    for path in files:
        n = ok = noaddr = mismatch = 0
        bad_samples = []
        with open(path, 'r', encoding='utf-8', errors='replace') as f:
            for line in f:
                m = LINE.match(line)
                if not m:
                    continue
                n += 1
                addr, typ, objpath = m.group(1).upper(), m.group(2), m.group(3)
                row = con.execute('SELECT type, path FROM obj WHERE addr=?', (addr,)).fetchone()
                if row is None:
                    noaddr += 1
                    if len(bad_samples) < 4:
                        bad_samples.append(f'  no such address {addr}  {typ} {objpath}')
                    continue
                if row[0] != typ or row[1] != objpath:
                    mismatch += 1
                    if len(bad_samples) < 4:
                        bad_samples.append(f'  mismatch {addr}: dump has {row[0]} {row[1]}')
                    continue
                ok += 1
        bad = noaddr + mismatch
        total_bad += bad
        print(f'{os.path.basename(path):38s} {n:7d} {ok:6d} {noaddr:8d} {mismatch:9d}')
        for s in bad_samples:
            print(s)
    print(f'\ntotal bad evidence lines: {total_bad}')
    return 0 if total_bad == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
