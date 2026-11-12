#!/usr/bin/env python
"""validate_mine.py - consistency checks for the data-mine artefacts and the master document.

  (a) every markdown file: no BOM, header/cell column counts agree
  (b) every `Name@0xNN` claim in the document must match saveobjects-matrix.tsv
  (c) every file named in the document must exist under research/mine
  (d) every `ObjectDump_allassets.txt:LINE` citation must be inside the dump's line range
  (e) every topic TSV: declared column count == row cell count

Run: python scripts\\validate_mine.py
Exit code 0 = all checks passed.
"""
import os
import re
import csv
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RESEARCH = os.path.dirname(HERE)
MINE = os.path.join(RESEARCH, 'mine')
DOC = os.path.join(MINE, 'SYNC-DATA-MINE.md')
DUMP_LINES = 2170742

errors = []
warnings = []


def check_encoding(path):
    with open(path, 'rb') as fh:
        if fh.read(3) == b'\xef\xbb\xbf':
            errors.append('%s: has UTF-8 BOM' % os.path.basename(path))


def check_tables(path):
    with open(path, encoding='utf-8') as fh:
        lines = fh.read().split('\n')
    bars = 0
    expect = None
    for i, line in enumerate(lines, 1):
        if not line.startswith('|'):
            expect = None
            continue
        n = line.count('|') - line.count('\\|')
        if expect is None:
            expect = n
            continue
        if n != expect:
            errors.append('%s:%d: table row has %d separators, header has %d'
                          % (os.path.basename(path), i, n, expect))
            bars += 1
            if bars > 5:
                return


def load_matrix():
    p = os.path.join(MINE, 'saveobjects-matrix.tsv')
    out = {}
    with open(p, encoding='utf-8') as fh:
        for r in csv.DictReader(fh, delimiter='\t'):
            for f in r['fields'].split('; '):
                if not f or '@' not in f:
                    continue
                nm, rest = f.split('@', 1)
                off = rest.split(':', 1)[0]
                out.setdefault(nm, set()).add(off.upper().replace('0X', '0x'))
    return out


def main():
    md = [os.path.join(MINE, n) for n in os.listdir(MINE) if n.endswith('.md')]
    for p in md:
        check_encoding(p)
        check_tables(p)
    print('(a) markdown files checked: %d' % len(md))

    doc = open(DOC, encoding='utf-8').read()

    # (b) Name@0xNN claims
    matrix = load_matrix()
    bad = 0
    for nm, off in re.findall(r'`([A-Za-z_][A-Za-z0-9_]*)@(0x[0-9A-Fa-f]+)`', doc):
        if nm not in matrix:
            continue
        if off.lower() not in {o.lower() for o in matrix[nm]}:
            errors.append('claim %s@%s not found in saveobjects-matrix (known: %s)'
                          % (nm, off, ', '.join(sorted(matrix[nm]))))
            bad += 1
    print('(b) Name@offset claims checked: %d bad' % bad)

    # (c) referenced artefact files
    refs = set(re.findall(r'mine\\\\([A-Za-z0-9._-]+\.[a-z]{2,4})', doc))
    refs |= set(re.findall(r'`([a-z0-9-]+\.tsv)`', doc))
    missing = []
    for r in refs:
        cand = os.path.join(MINE, r)
        if not os.path.exists(cand):
            # allow topic wildcard mentions like topic-01..19-*.props.tsv
            if '*' in r or '..' in r:
                continue
            missing.append(r)
    for m in missing:
        errors.append('referenced artefact missing: %s' % m)
    print('(c) referenced artefacts: %d checked, %d missing' % (len(refs), len(missing)))

    # (d) dump line citations
    cites = [int(x) for x in re.findall(r'ObjectDump_allassets\.txt:(\d+)', doc)]
    badc = [c for c in cites if not (0 < c <= DUMP_LINES)]
    for c in badc:
        errors.append('citation out of range: %d' % c)
    print('(d) dump-line citations: %d checked, %d out of range' % (len(cites), len(badc)))

    # (e) topic tsv column consistency
    badtsv = 0
    for n in sorted(os.listdir(MINE)):
        if not n.endswith('.tsv'):
            continue
        with open(os.path.join(MINE, n), encoding='utf-8') as fh:
            head = fh.readline().rstrip('\n').split('\t')
            for i, line in enumerate(fh, 2):
                cells = line.rstrip('\n').split('\t')
                if len(cells) != len(head):
                    errors.append('%s:%d: %d cells, header %d' % (n, i, len(cells), len(head)))
                    badtsv += 1
                    break
    print('(e) tsv files: %d bad' % badtsv)

    # (f) every markdown table row of the form  | `Member` | ... | `0xNN` |  must exist for a class
    #     whose name appears in the ~60 lines above the table (heading/paragraph context)
    import sqlite3
    c = sqlite3.connect(os.path.join(RESEARCH, 'raw', 'index.sqlite3'))
    c.row_factory = sqlite3.Row

    def props_of(cls):
        r = c.execute("SELECT addr FROM obj WHERE path=? AND type IN "
                      "('Class','BlueprintGeneratedClass','ScriptStruct')", (cls,)).fetchone()
        if not r:
            return None
        out = {}
        for row in c.execute("SELECT path,off FROM obj WHERE owner=? AND off IS NOT NULL", (r['addr'],)):
            out.setdefault(row['path'].rsplit(':', 1)[-1], set()).add(int(row['off']))
        return out

    def resolve(token):
        """identifier or path -> list of candidate class paths"""
        if token.startswith('/'):
            return [token] if props_of(token) is not None else []
        cands = []
        for pre in ('/Script/Sparta.', '/Script/Sparta.'):
            p = pre + token
            if props_of(p) is not None:
                cands.append(p)
        for pre in ('U', 'A', 'F'):
            if token.startswith(pre) and len(token) > 2:
                p = '/Script/Sparta.' + token[1:]
                if props_of(p) is not None:
                    cands.append(p)
        if token.endswith('_C'):
            row = c.execute("SELECT path FROM obj WHERE type IN "
                            "('BlueprintGeneratedClass','Class') AND path LIKE ? LIMIT 1",
                            ('%.' + token,)).fetchone()
            if row:
                cands.append(row['path'])
        return cands

    lines = doc.split('\n')
    checked = 0
    ctx = []
    i = 0
    while i < len(lines):
        line = lines[i]
        if not line.startswith('|'):
            toks = re.findall(r'`(/[^`]+)`|\b(U?[A-Z][A-Za-z0-9_]{3,}|[A-Z]{2,}[A-Za-z0-9_]*_C)\b', line)
            found = []
            for a, b in toks:
                t = a or b
                if not t:
                    continue
                found.extend(resolve(t))
            if found:
                ctx = (ctx + found)[-6:]
            i += 1
            continue
        # a table block: header, separator, rows
        j = i
        block = []
        while j < len(lines) and lines[j].startswith('|'):
            block.append(lines[j])
            j += 1
        header = [x.strip().lower() for x in block[0].split('|')[1:-1]]
        skip = set()
        for k, h in enumerate(header):
            if 'size' in h or 'read at' in h or 'hint' in h:
                skip.add(k)
        ctxprops = [p for p in (props_of(cl) for cl in ctx) if p]
        for row in block[2:]:
            cells = [x.strip() for x in row.split('|')[1:-1]]
            for k, cell in enumerate(cells):
                if k in skip:
                    continue
                offs = [int(o, 16) for o in re.findall(r'`(0x[0-9A-Fa-f]+)`', cell)]
                if not offs:
                    continue
                if len(offs) > 1:
                    continue  # many-to-many row (e.g. a list of delegates); not 1:1 checkable
                names = [m for c2 in cells for m in re.findall(r'`([A-Za-z_][A-Za-z0-9_]*)`', c2)]
                for o in offs:
                    for nm in names:
                        for cp in ctxprops:
                            if nm in cp:
                                checked += 1
                                if o not in cp[nm]:
                                    warnings.append('doc table line %d: %s has no member at 0x%X '
                                                    '(known: %s) - context %s'
                                                    % (i + 1, nm, o,
                                                       ', '.join('0x%X' % x for x in sorted(cp[nm])),
                                                       ctx[-1].rsplit('/', 1)[-1]))
                                break
        i = j
    print('(f) doc table member/offset pairs checked: %d, %d suspicious' % (checked, len(warnings)))

    # (g) main.lua line citations
    lua = os.path.join(RESEARCH, '..', 'ms2-coop', 'mod', 'CoopMod', 'Scripts', 'main.lua')
    nlines = 0
    if os.path.exists(lua):
        nlines = sum(1 for _ in open(lua, encoding='utf-8', errors='replace'))
    for m in re.findall(r'main\.lua:(\d+)(?:-(\d+))?', doc):
        for g in m:
            if g and not (0 < int(g) <= nlines):
                errors.append('main.lua citation out of range: %s (file has %d lines)' % (g, nlines))
    print('(g) main.lua citations checked against %d lines' % nlines)

    print()
    if warnings:
        print('WARNINGS (%d):' % len(warnings))
        for w in warnings[:20]:
            print('  ~', w)
    if errors:
        print('FAILED (%d):' % len(errors))
        for e in errors[:40]:
            print('  -', e)
        return 1
    print('ALL CHECKS PASSED')
    return 0


if __name__ == '__main__':
    sys.exit(main())
