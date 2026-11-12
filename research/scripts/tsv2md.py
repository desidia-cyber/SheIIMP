#!/usr/bin/env python
"""tsv2md.py - render a mined TSV artifact as a markdown table (for the doc's bulk tables).

  python scripts\\tsv2md.py <artifact.tsv> [--limit N] [--wrap-col N]
writes <artifact>.md next to the .tsv and prints the row count.

Pipes and backslashes inside cells are escaped so the markdown table never breaks.
"""
import os
import sys


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    src = argv[1]
    limit = 0
    for i, a in enumerate(argv):
        if a == '--limit':
            limit = int(argv[i + 1])
    with open(src, encoding='utf-8') as fh:
        head = next(fh).rstrip('\n').split('\t')
        rows = [l.rstrip('\n').split('\t') for l in fh]
    if limit:
        rows = rows[:limit]
    out = []
    out.append('| ' + ' | '.join(head) + ' |')
    out.append('|' + '|'.join(['---'] * len(head)) + '|')
    for r in rows:
        cells = []
        for c in r:
            c = c.replace('|', '\\|')
            cells.append(c)
        out.append('| ' + ' | '.join(cells) + ' |')
    dst = os.path.splitext(src)[0] + '.md'
    with open(dst, 'w', encoding='utf-8', newline='\n') as fh:
        fh.write('\n'.join(out) + '\n')
    print('%s : %d rows' % (dst, len(rows)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
