#!/usr/bin/env python
"""mine_extra.py - the non-topic artifacts of the sync data mine.

  enums-sparta.tsv        Sparta module enums with explicit values (Dumper-7)
  structs-sparta.tsv      Sparta module struct layouts with offsets (Dumper-7)
  native-classes.tsv      every /Script/Sparta.* UClass with its super chain
  savecomponent-owners.tsv  every class that owns a USpartaSaveComponent
  live-objects.tsv        per-class object counts in the live gameplay dump
  datatables.tsv          every DataTable asset (row names are runtime-only)
  item-assets.tsv         item-definition classes / blueprints under /Game/Sparta/Items
  hashes.tsv              SHA-256 of every mined artifact + of the primary sources

Run: python scripts\\mine_extra.py
"""
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RESEARCH = os.path.dirname(HERE)
MINE = os.path.join(RESEARCH, 'mine')
sys.path.insert(0, HERE)

import mine  # noqa: E402


def sha256(path, chunk=1 << 20):
    h = hashlib.sha256()
    with open(path, 'rb') as fh:
        while True:
            b = fh.read(chunk)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def main():
    os.makedirs(MINE, exist_ok=True)
    c = mine.db('index.sqlite3')
    ci = mine.db('index_ingame.sqlite3')

    # ---- enums / structs already produced by mine.py enums|structs ; emit Sparta-only views
    for src, dst in (('enums.tsv', 'enums-sparta.tsv'), ('structs.tsv', 'structs-sparta.tsv')):
        sp = os.path.join(MINE, src)
        if not os.path.exists(sp):
            print('skip (missing)', src)
            continue
        rows = []
        with open(sp, 'r', encoding='utf-8') as fh:
            head = next(fh)
            cols = head.rstrip('\n').split('\t')
            idx = cols.index('module')
            for line in fh:
                f = line.rstrip('\n').split('\t')
                if len(f) > idx and f[idx] in ('Sparta', 'SpartaSettingsSaveObject'):
                    rows.append(f)
        with open(os.path.join(MINE, dst), 'w', encoding='utf-8', newline='\n') as fh:
            fh.write(head)
            for r in rows:
                fh.write('\t'.join(r) + '\n')
        print('%-24s %d rows' % (dst, len(rows)))

    # ---- native Sparta classes with super chain
    rows = []
    for r in c.execute("SELECT path,super FROM obj WHERE type='Class' AND path LIKE '/Script/Sparta.%' ORDER BY path"):
        sup = mine.resolve(c, r['super'])
        rows.append((r['path'].split('.', 1)[1], r['path'], sup))
    mine.write_tsv('native-classes.tsv', ('name', 'path', 'super'), rows)
    print('native-classes.tsv       %d rows' % len(rows))

    # ---- classes owning a SpartaSaveComponent (SCS template rows of each BP class)
    owners = {}
    for r in c.execute("""SELECT path,line FROM obj
                          WHERE type='SpartaSaveComponent' AND path LIKE '%:SpartaSave%'
                          ORDER BY path"""):
        cls = r['path'].split(':', 1)[0]
        owners.setdefault(cls, r['line'])
    mine.write_tsv('savecomponent-owners.tsv', ('class', 'first_evidence_line'),
                   sorted(owners.items()))
    print('savecomponent-owners.tsv %d rows' % len(owners))

    # ---- live object census from the gameplay dump
    live = {}
    for r in ci.execute("SELECT type,COUNT(*) c FROM obj GROUP BY type ORDER BY c DESC"):
        live[r['type']] = r['c']
    mine.write_tsv('live-objects.tsv', ('class_or_type', 'object_count_in_gameplay_dump'),
                   sorted(live.items(), key=lambda kv: -kv[1]))
    print('live-objects.tsv         %d rows' % len(live))

    # ---- data tables
    rows = []
    for r in c.execute("SELECT path,line FROM obj WHERE type='DataTable' ORDER BY path"):
        rows.append((r['path'], r['line']))
    mine.write_tsv('datatables.tsv', ('path', 'dump_line'), rows)
    print('datatables.tsv           %d rows' % len(rows))

    # ---- item definition surface
    rows = []
    for r in c.execute("""SELECT path,type,super FROM obj WHERE type IN
                          ('BlueprintGeneratedClass','Class','Blueprint')
                          AND path LIKE '/Game/Sparta/Items/%' ORDER BY path"""):
        rows.append((r['path'], r['type'], mine.resolve(c, r['super'])))
    mine.write_tsv('item-assets.tsv', ('path', 'type', 'super'), rows)
    print('item-assets.tsv          %d rows' % len(rows))

    # ---- save-object matrix: one row per save-object class, compact field list
    rows = []
    ct = ('Class', 'BlueprintGeneratedClass', 'WidgetBlueprintGeneratedClass',
          'AnimBlueprintGeneratedClass')
    q = "SELECT o.path,o.super FROM obj o WHERE o.type IN (%s)" % ','.join('?' * len(ct))
    paths = [(r['path'], mine.resolve(c, r['super'])) for r in c.execute(q, ct)]
    sup = {p: s for p, s in paths}

    def ancestors(p):
        out, cur = [], p
        for _ in range(24):
            s = sup.get(cur)
            if not s or s == cur:
                break
            out.append(s)
            cur = s
        return out

    for p in sorted(sup):
        anc = ancestors(p)
        if not any(a in ('/Script/Sparta.SpartaSaveObject',
                         '/Script/Sparta.PlayerSaveObject',
                         '/Script/Sparta.SpartaSessionSaveObjectBase',
                         '/Script/Sparta.PlayerProgressionSaveData',
                         '/Script/Sparta.TarstoneSaveData',
                         '/Script/Sparta.PPItemsSaveData',
                         '/Script/Sparta.DataLayersSave') for a in anc):
            continue
        own = [(nm, t, off) for decl, nm, t, off, line in mine.flatten(c, p)]
        fields = '; '.join('%s@%s:%s' % (nm, off, t.replace('Property of ', ''))
                           for nm, t, off in own)
        rows.append((p, anc[0] if anc else '', len(own), fields))
    mine.write_tsv('saveobjects-matrix.tsv',
                   ('class', 'super', 'field_count', 'fields'), rows)
    print('saveobjects-matrix.tsv   %d rows' % len(rows))

    # ---- hashes
    hrows = []
    for n in sorted(os.listdir(MINE)):
        p = os.path.join(MINE, n)
        if os.path.isfile(p) and n != 'hashes.tsv':
            hrows.append((n, os.path.getsize(p), sha256(p)))
    for p, label in ((os.path.join(RESEARCH, 'raw', 'index.sqlite3'), 'raw/index.sqlite3'),
                     (os.path.join(RESEARCH, 'raw', 'index_ingame.sqlite3'), 'raw/index_ingame.sqlite3')):
        if os.path.exists(p):
            hrows.append((label, os.path.getsize(p), sha256(p)))
    mine.write_tsv('hashes.tsv', ('artifact', 'bytes', 'sha256'), hrows)
    print('hashes.tsv               %d rows' % len(hrows))


if __name__ == '__main__':
    main()
