#!/usr/bin/env python3
"""Repair or audit evidence lines in the research reports.

Each report quotes dump lines starting with `[ADDRESS] Type /path ...`.  The SQLite index stores
the bracket-field of each object line *stripped* of its leading space, so lines that were rebuilt
from SQL are missing exactly one space before `[n:` / `[o:` and are therefore not byte-identical to
`UE4SS_ObjectDump.txt`.  This tool rewrites every evidence line from the index, so the quoted line
becomes exact by construction, and reports any line whose address/type/path does not exist in the
dump (i.e. a line that was transcribed or invented).

  python repair_evidence.py --check      # only report
  python repair_evidence.py --apply      # rewrite the files
"""
import glob
import os
import re
import sqlite3
import sys

LINE = re.compile(r'^(\s*)\[([0-9A-Fa-f]{8,16})\]\s+(\S+)\s+(\S+)(.*)$')
HERE = os.path.dirname(os.path.abspath(__file__))
RESEARCH = os.path.dirname(HERE)


def split_line(m):
    """Return (lead, addr, type, path, tail).

    A dump line is `[addr] Type /path [n: ..] ..`; the indexer stored the path as a whitespace-free
    token, so Blueprint variables whose name contains spaces (e.g. `AsBPSO Shepherd Sheep Gate`)
    are stored truncated at the first space and the remainder lives in `rest`.  Lines rebuilt from
    SQL are also missing the space before `[n:`/`[o:`.  Both are undone here.
    """
    lead, addr, typ, pathtok, tail = m.groups()
    if '[' in pathtok:
        i = pathtok.index('[')
        return lead, addr, typ, pathtok[:i], pathtok[i:] + tail
    return lead, addr, typ, pathtok, tail


def main():
    apply = '--apply' in sys.argv
    files = [a for a in sys.argv[1:] if not a.startswith('--')] or \
        sorted(glob.glob(os.path.join(RESEARCH, '*.md')))
    conA = sqlite3.connect(os.path.join(RESEARCH, 'raw', 'index.sqlite3'))
    conB = None
    ingame = os.path.join(RESEARCH, 'raw', 'index_ingame.sqlite3')
    if os.path.exists(ingame):
        conB = sqlite3.connect(ingame)

    def lookup(addr):
        for con in (conA, conB):
            if con is None:
                continue
            row = con.execute('SELECT type, path, rest FROM obj WHERE addr=?', (addr,)).fetchone()
            if row:
                return row
        return None

    grand = {'fixed': 0, 'ok': 0, 'wrong': 0, 'unknown': 0}
    for path in files:
        with open(path, 'r', encoding='utf-8', errors='replace') as f:
            lines = f.read().split('\n')
        counts = dict.fromkeys(['fixed', 'ok', 'wrong', 'unknown'], 0)
        wrong_samples = []
        out = []
        for line in lines:
            m = LINE.match(line)
            if not m:
                out.append(line)
                continue
            lead, addr, typ, objpath, tail = split_line(m)
            addr = addr.upper()
            row = lookup(addr)
            if row is None:
                counts['unknown'] += 1
                out.append(line)
                continue
            dbtype, dbpath, dbrest = row
            if dbtype != typ or dbpath != objpath:
                counts['wrong'] += 1
                if len(wrong_samples) < 6:
                    wrong_samples.append(f'    {addr}: report says {typ} {objpath}\n'
                                         f'    {" " * len(addr)}  dump   has {dbtype} {dbpath}')
                out.append(line)
                continue
            exact = f'{lead}[{addr}] {dbtype} {dbpath}' + ((' ' + dbrest) if dbrest else '')
            if exact != line:
                counts['fixed'] += 1
                out.append(exact)
            else:
                counts['ok'] += 1
                out.append(line)
        for k in counts:
            grand[k] += counts[k]
        print(f'{os.path.basename(path):38s} exact={counts["ok"]:5d} respaced={counts["fixed"]:5d} '
              f'WRONG={counts["wrong"]:4d} not-in-dump={counts["unknown"]:4d}')
        for s in wrong_samples:
            print(s)
        if apply and counts['fixed']:
            with open(path, 'w', encoding='utf-8', newline='\n') as f:
                f.write('\n'.join(out))
    print(f'\ntotal: exact={grand["ok"]} respaced={grand["fixed"]} wrong={grand["wrong"]} '
          f'not-in-dump={grand["unknown"]}   {"APPLIED" if apply else "check only"}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
