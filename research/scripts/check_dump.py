#!/usr/bin/env python3
"""Sanity-check a UE4SS object dump: line count, unmatched lines, type histogram,
package histogram.  Used for research/00-sources.md.

Usage: python check_dump.py <dump.txt> [--top 30]
"""
import collections
import re
import sys

LINE_RE = re.compile(r'^\[([0-9A-Fa-f]+)\]\s+(\S+)\s+(\S+)(.*)$')


def main():
    path = sys.argv[1]
    top = 30
    if '--top' in sys.argv:
        top = int(sys.argv[sys.argv.index('--top') + 1])
    n = 0
    unmatched = 0
    samples = []
    types = collections.Counter()
    pkgs = collections.Counter()
    classes = collections.Counter()
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        for line in f:
            n += 1
            m = LINE_RE.match(line)
            if not m:
                unmatched += 1
                if len(samples) < 10:
                    samples.append(line.rstrip()[:200])
                continue
            typ, objpath = m.group(2), m.group(3)
            types[typ] += 1
            if objpath.startswith('/'):
                pkg = objpath.split(':')[0].rsplit('.', 1)[0]
                pkgs[pkg] += 1
            elif typ == 'Class':
                classes[objpath] += 1
    print(f'file      {path}')
    print(f'lines     {n}')
    print(f'unmatched {unmatched}')
    for s in samples:
        print('   unmatched sample:', s)
    print('top object types:')
    for t, c in types.most_common(top):
        print(f'   {c:9d}  {t}')
    print('top packages by object count:')
    for p, c in pkgs.most_common(top):
        print(f'   {c:9d}  {p}')


if __name__ == '__main__':
    main()
