#!/usr/bin/env python3
"""Index a UE4SS object dump (UE4SS_ObjectDump.txt) into SQLite for fast querying.

Dump line shape (UE4SS 3.x, UE 5.6):

  [00007FF43084A3B0] Package /Script/CoreUObject [n: 5C6] [c: ..] [or: ..]
  [00007FF430520070] Class /Script/CoreUObject.Object [n: ..] [c: ..] [or: ..] [sps: ..]
  [00007FF4309BEAB8] Function /Script/CoreUObject.Object:ExecuteUbergraph [n: ..] [c: ..] [or: ..] [f: ..]
  [00000290F15BBF40] IntProperty /Script/CoreUObject.Object:ExecuteUbergraph:EntryPoint [o: 0] [n: ..] [c: ..] [owr: 00007FF4309BEAB8]

  * `or:`  = Outer  (owning package / class / function)
  * `owr:` = Outer  (same thing, written differently for objects that have one)
  * `sps:` = SuperStruct (a class' parent class)
  * `ss:`  = StructProperty's struct type,  `pc:` = ObjectProperty's class,
    `ai:`  = ArrayProperty's inner type,   `em:` = EnumProperty's enum.
  * `o:`   = member offset inside the owning class/struct/parameter frame.

The object path itself is hierarchical with ':':
  /Script/Engine.Actor              a class (package /Script/Engine)
  /Script/Engine.Actor:Tags         a member
  /Script/Engine.Actor:K2_DestroyActor:ReturnValue   a parameter of a function

Usage:
  python parse_dump.py index <dump.txt> <out.sqlite3> [--max-lines N]
  python parse_dump.py classes <db> [--regex PAT] [--under /Game/Sparta]
  python parse_dump.py show <db> <path-substring>
  python parse_dump.py find <db> <regex> [--types Class,Function]
  python parse_dump.py members <db> <class-path-substring>
"""
import argparse
import os
import re
import sqlite3
import sys

LINE_RE = re.compile(r'^\[([0-9A-Fa-f]+)\]\s+(\S+)\s+(\S+)(.*)$')
# a handful of lines (158 in the 2026-10-22 all-assets dump) are written without the
# address prefix:  "DoubleProperty Name./Script/Engine.Spline:X:Y"
ALT_RE = re.compile(r'^(\S+Property)\s+(\S+?)\.(/\S+)$')
ATTR_RE = re.compile(r'\[([a-z]{1,4}):\s*([0-9A-Fa-f]+)\]')


def iter_objects(fp, max_lines=None):
    n = 0
    for line in fp:
        n += 1
        if max_lines and n > max_lines:
            return
        m = LINE_RE.match(line)
        if not m:
            a = ALT_RE.match(line.rstrip())
            if a:
                yield ('NA%07d' % n, a.group(1), a.group(3), None, None, None, None, '', n)
            continue
        addr, typ, path, rest = m.group(1), m.group(2), m.group(3), m.group(4)
        a = dict(ATTR_RE.findall(rest))
        yield (addr.upper(), typ, path, a.get('owr') or a.get('or'),
               a.get('sps'), a.get('ss') or a.get('pc') or a.get('ai') or a.get('em'),
               int(a['o'], 16) if 'o' in a else None, rest.strip(), n)


SCHEMA = """
CREATE TABLE obj (
  addr TEXT PRIMARY KEY,
  type TEXT NOT NULL,
  path TEXT NOT NULL,
  owner TEXT,
  super TEXT,
  ref TEXT,
  off INTEGER,
  rest TEXT,
  line INTEGER
);
CREATE INDEX i_owner ON obj(owner);
CREATE INDEX i_type ON obj(type);
CREATE INDEX i_path ON obj(path);
CREATE VIEW cls AS
  SELECT addr, type, path, super FROM obj
  WHERE type = 'Class' OR type LIKE '%GeneratedClass';
"""


def cmd_index(args):
    dump, out = args.dump, args.out
    if os.path.exists(out):
        os.remove(out)
    con = sqlite3.connect(out)
    con.execute('PRAGMA journal_mode=OFF')
    con.execute('PRAGMA synchronous=OFF')
    con.executescript(SCHEMA)
    rows = []
    total = 0
    with open(dump, 'r', encoding='utf-8', errors='replace') as fp:
        for rec in iter_objects(fp, args.max_lines):
            rows.append(rec)
            total += 1
            if len(rows) >= 20000:
                con.executemany('INSERT OR REPLACE INTO obj VALUES (?,?,?,?,?,?,?,?,?)', rows)
                rows = []
                if total % 200000 == 0:
                    print(f'  {total} objects...', file=sys.stderr)
    if rows:
        con.executemany('INSERT OR REPLACE INTO obj VALUES (?,?,?,?,?,?,?,?,?)', rows)
    con.commit()
    n = con.execute('SELECT COUNT(*) FROM obj').fetchone()[0]
    con.close()
    print(f'indexed {n} objects from {dump} -> {out}')


def owner_path(path):
    i = path.rfind(':')
    return path[:i] if i > 0 else None


def member_name(path):
    i = path.rfind(':')
    return path[i + 1:] if i > 0 else None


def cmd_classes(args):
    con = sqlite3.connect(args.db)
    rx = re.compile(args.regex, re.I) if args.regex else None
    q = ('SELECT c.type, c.path, s.type, s.path FROM cls c '
         'LEFT JOIN obj s ON s.addr = UPPER(c.super)')
    params = []
    if args.under:
        q += ' WHERE c.path LIKE ?'
        params.append(args.under + '%')
    rows = con.execute(q + ' ORDER BY c.path', params).fetchall()
    for typ, path, styp, spath in rows:
        if rx and not rx.search(path):
            continue
        print(f'{path}\t{typ}\t{spath or ""}')
    print(f'# {len(rows)} classes')


def cmd_show(args):
    """Print a class/struct/function and all of its members, grouped."""
    con = sqlite3.connect(args.db)
    needle = args.what
    rows = con.execute('SELECT addr, type, path, super, ref FROM obj WHERE path LIKE ? ORDER BY type, path',
                       ('%' + needle + '%',)).fetchall()
    if not rows:
        print('no object path matches', needle, file=sys.stderr)
        return
    for addr, typ, path, sp, ref in rows:
        print(f'### {typ} {path}  [{addr}]')
        if sp:
            prow = con.execute('SELECT path FROM obj WHERE addr=?', (sp.upper(),)).fetchone()
            print(f'    super: {prow[0] if prow else sp}')
        kids = con.execute('SELECT addr, type, path, off, ref FROM obj WHERE owner=? ORDER BY line',
                           (addr,)).fetchall()
        if kids:
            print(f'    {len(kids)} members:')
            for child_addr, ktyp, kpath, koff, kref in kids:
                extra = ''
                if kref:
                    r = con.execute('SELECT path FROM obj WHERE addr=?', (kref.upper(),)).fetchone()
                    extra = ' of ' + (r[0] if r else kref)
                off = f'[o:{koff:#x}]' if koff is not None else ''
                if ktyp == 'Function':
                    print(f'      FUN {member_name(kpath)} {off}')
                    for ptyp, ppath, poff, pref in con.execute(
                            'SELECT type, path, off, ref FROM obj WHERE owner=? ORDER BY line', (child_addr,)):
                        pex = ''
                        if pref:
                            r = con.execute('SELECT path FROM obj WHERE addr=?', (pref.upper(),)).fetchone()
                            pex = ' of ' + (r[0] if r else pref)
                        po = f'[o:{poff:#x}]' if poff is not None else ''
                        print(f'          param {member_name(ppath)} : {ptyp}{pex} {po}')
                else:
                    print(f'      PRP {member_name(kpath)} : {ktyp}{extra} {off}')
        print()


def cmd_find(args):
    con = sqlite3.connect(args.db)
    types = args.types.split(',') if args.types else None
    rx = re.compile(args.regex, re.I)
    q = 'SELECT type, path FROM obj'
    params = []
    if types:
        q += ' WHERE type IN (%s)' % ','.join('?' * len(types))
        params = types
    q += ' ORDER BY path'
    for typ, path in con.execute(q, params):
        if rx.search(path):
            print(f'{typ}\t{path}')


def cmd_members(args):
    """Every member (property/function/parameter) whose owner path matches."""
    con = sqlite3.connect(args.db)
    rows = con.execute(
        'SELECT o.type, o.path, o.off, o.ref, p.type, p.path FROM obj o '
        'JOIN obj p ON o.owner = p.addr WHERE p.path LIKE ? ORDER BY p.path, o.line',
        ('%' + args.what + '%',)).fetchall()
    for ktyp, kpath, koff, kref, ptyp, ppath in rows:
        print(f'{ptyp}:{ppath}\t{kpath}\t{ktyp}\t{koff}')


def cmd_raw(args):
    """Print the original dump lines (rebuilt byte-for-byte) of matching objects."""
    con = sqlite3.connect(args.db)
    types = args.types.split(',') if args.types else None
    rx = re.compile(args.regex, re.I)
    q = 'SELECT addr, type, path, rest, line FROM obj'
    params = []
    if types:
        q += ' WHERE type IN (%s)' % ','.join('?' * len(types))
        params = list(types)
    q += ' ORDER BY line'
    n = 0
    for addr, typ, path, rest, line in con.execute(q, params):
        if not rx.search(path):
            continue
        print('[{0}] {1} {2}{3}'.format(addr, typ, path, rest or ''))
        n += 1
        if args.limit and n >= args.limit:
            print(f'# truncated at {args.limit} lines', file=sys.stderr)
            break


def cmd_fun(args):
    """Functions (with their parameters in dump order) of matching classes."""
    con = sqlite3.connect(args.db)
    rx = re.compile(args.regex, re.I)
    classes = con.execute(
        'SELECT addr, type, path FROM obj WHERE type IN ("Class","Struct","Function","DelegateFunction") ORDER BY path').fetchall()
    def resolve(ref):
        if not ref:
            return ''
        r = con.execute('SELECT path FROM obj WHERE addr=?', (ref.upper(),)).fetchone()
        return r[0] if r else ref
    for addr, typ, path in classes:
        if not rx.search(path):
            continue
        funs = con.execute('SELECT addr, path FROM obj WHERE owner=? AND type IN ("Function","DelegateFunction") ORDER BY line',
                           (addr,)).fetchall()
        if not funs:
            continue
        print(f'## {typ} {path}  [{addr}]')
        for faddr, fpath in funs:
            ps = con.execute('SELECT type, path, off, ref FROM obj WHERE owner=? ORDER BY line', (faddr,)).fetchall()
            sig = ', '.join(
                '{n}: {t}{r}{o}'.format(
                    n=member_name(pp), t=pt, r=(' of ' + resolve(pref)) if pref else '',
                    o=(f' [o:{poff:#x}]' if poff is not None else ''))
                for pt, pp, poff, pref in ps)
            print(f'   {member_name(fpath)}({sig})')
        print()


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='cmd', required=True)

    p = sub.add_parser('index')
    p.add_argument('dump')
    p.add_argument('out')
    p.add_argument('--max-lines', type=int, default=0)
    p.set_defaults(func=cmd_index)

    p = sub.add_parser('classes')
    p.add_argument('db')
    p.add_argument('--regex')
    p.add_argument('--under')
    p.add_argument('--with-parents', action='store_true')
    p.set_defaults(func=cmd_classes)

    p = sub.add_parser('show')
    p.add_argument('db')
    p.add_argument('what')
    p.set_defaults(func=cmd_show)

    p = sub.add_parser('find')
    p.add_argument('db')
    p.add_argument('regex')
    p.add_argument('--types')
    p.set_defaults(func=cmd_find)

    p = sub.add_parser('members')
    p.add_argument('db')
    p.add_argument('what')
    p.set_defaults(func=cmd_members)

    p = sub.add_parser('raw')
    p.add_argument('db')
    p.add_argument('regex')
    p.add_argument('--types')
    p.add_argument('--limit', type=int, default=0)
    p.set_defaults(func=cmd_raw)

    p = sub.add_parser('fun')
    p.add_argument('db')
    p.add_argument('regex')
    p.set_defaults(func=cmd_fun)

    args = ap.parse_args()
    args.func(args)


if __name__ == '__main__':
    main()
