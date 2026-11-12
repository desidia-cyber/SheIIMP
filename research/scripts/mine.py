#!/usr/bin/env python
"""mine.py - reproducible extractor for the MS2 co-op sync data mine.

Sources (all already on disk, nothing is re-dumped):
  raw/index.sqlite3        SQLite index of ObjectDump_allassets.txt (UE4SS object dump, menu, LoadAllAssets=1)
  raw/index_ingame.sqlite3 SQLite index of ObjectDump_ingame.txt (UE4SS object dump, live gameplay)
  raw/CXXHeaderDump/*.hpp  UE4SS CXX generator headers (native member offsets)
  raw/Dumper7_*/(...)/CppSDK/SDK/*.hpp|*.cpp  Dumper-7 SDK (enum values, struct/class offsets, func flags)

Output: research/mine/*.tsv + sync-data.json  (written by `python scripts\\mine.py gen`)

Subcommands
  cls    <regex>                 classes/structs whose path matches regex
  show   <exact-path>            members of one object (own only), with resolved types
  tree   <exact-path>            inheritance chain, members per level
  flat   <regex>                 every class matching regex -> flattened property list (own+inherited)
  fun    <regex>                 every class matching regex -> functions with ordered params
  gen                            emit the full artifact set (topics + enums + structs + flags)
  enums                          enum values from Dumper-7 `_structs.hpp`
  structs                        struct layouts from Dumper-7 `_structs.hpp`
  flags                          UFunction flags from Dumper-7 `_functions.cpp`

Notes
  * `obj.off` is the hex member offset stored as a decimal integer -> printed as 0xNN.
  * `obj.ref` is the address of the referenced type (StructProperty struct, ObjectProperty
    class, ArrayProperty inner, EnumProperty enum) -> resolved via a self-join.
  * parameter rows are properties whose owner is a Function; their `off` is the offset inside
    the generated Parms struct, so ordering is by dump order (= declaration order).
  * compiler temporaries are filtered from parameter lists (documented rule, see
    scripts/QUERYING.md): Temp_*, CallFunc_*, K2Node_*, __Internal_*, ExecuteUbergraph_*,
    Goto_*, eb_*, bpf_*, ubergraph*, ReturnValue is kept (it is the real return value).
"""
import argparse
import json
import os
import re
import sqlite3
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RESEARCH = os.path.dirname(HERE)
RAW = os.path.join(RESEARCH, 'raw')
MINE = os.path.join(RESEARCH, 'mine')

TEMP_PREFIXES = (
    'Temp_', 'CallFunc_', 'K2Node_', '__Internal_', 'ExecuteUbergraph_',
    'Goto_', 'eb_', 'bpf_', 'ubergraph', 'K2Node',
)

CLASS_TYPES = (
    'Class', 'BlueprintGeneratedClass', 'WidgetBlueprintGeneratedClass',
    'AnimBlueprintGeneratedClass', 'ScriptStruct', 'UserDefinedStruct',
    'UserDefinedEnum', 'Enum', 'Blueprint', 'DynamicClass',
)


def db(name='index.sqlite3'):
    p = os.path.join(RAW, name)
    if not os.path.exists(p):
        sys.exit('missing index: ' + p)
    c = sqlite3.connect('file:%s?mode=ro' % p.replace('\\', '/'), uri=True)
    c.row_factory = sqlite3.Row
    return c


def hx(v):
    if v is None:
        return ''
    return '0x%X' % int(v)


def is_temp(name):
    return any(name.startswith(p) for p in TEMP_PREFIXES)


def find_obj(c, path, types=None):
    """exact path -> (addr, type, super_addr)"""
    q = 'SELECT addr,type,super FROM obj WHERE path=?'
    args = [path]
    if types:
        q += ' AND type IN (%s)' % ','.join('?' * len(types))
        args += list(types)
    return c.execute(q + ' LIMIT 1', args).fetchone()


def resolve(c, addr):
    if not addr:
        return ''
    r = c.execute('SELECT path FROM obj WHERE addr=? LIMIT 1', (addr,)).fetchone()
    return (r[0] if r else '')


def own_members(c, addr):
    q = """SELECT o.type,o.path,o.off,o.ref,o.rest,o.line,r.path AS refd
           FROM obj o LEFT JOIN obj r ON r.addr=UPPER(o.ref)
           WHERE o.owner=? ORDER BY o.line"""
    return c.execute(q, (addr,)).fetchall()


def pretty_ref(r):
    """Human type string: 'StructProperty of /Script/Sparta.X'."""
    t, refd = r['type'], r['refd']
    if not refd:
        return t
    if 'Property' in t:
        return '%s of %s' % (t, refd)
    return '%s (%s)' % (t, refd)


def params_of(c, func_addr):
    out = []
    for r in c.execute("""SELECT o.type,o.path,o.off,o.rest,r.path AS refd
                          FROM obj o LEFT JOIN obj r ON r.addr=UPPER(o.ref)
                          WHERE o.owner=? ORDER BY o.line""", (func_addr,)):
        nm = r['path'].rsplit(':', 1)[-1]
        if is_temp(nm):
            continue
        out.append((nm, pretty_ref(r), hx(r['off'])))
    return out


def chain(c, path):
    """[(path, addr, super_path)] from the class upward."""
    out = []
    seen = set()
    cur = path
    while cur and cur not in seen:
        seen.add(cur)
        r = find_obj(c, cur, CLASS_TYPES)
        if not r:
            break
        sup = resolve(c, r[2])
        out.append((cur, r[0], sup))
        cur = sup
    return out


def flatten(c, path):
    """[(declaring_class, name, type_string, offset_hex)] own-first, dedup by name."""
    rows = []
    seen = set()
    for cls, addr, _sup in chain(c, path):
        for m in own_members(c, addr):
            if 'Property' not in m['type']:
                continue  # functions/delegates handled elsewhere
            nm = m['path'].rsplit(':', 1)[-1]
            if nm in seen:
                continue
            seen.add(nm)
            rows.append((cls, nm, pretty_ref(m), hx(m['off']), m['line']))
    return rows


def funcs(c, path):
    rows = []
    for cls, addr, _sup in chain(c, path):
        for m in own_members(c, addr):
            if m['type'] != 'Function':
                continue
            nm = m['path'].rsplit(':', 1)[-1]
            rows.append((cls, nm, m['line'], params_of(c, resolve_addr(c, m['path']))))
    return rows


def resolve_addr(c, path):
    r = c.execute("SELECT addr FROM obj WHERE path=? AND type='Function' LIMIT 1", (path,)).fetchone()
    return r['addr'] if r else None


# ----------------------------------------------------------------------------- source parsers

D7_ROOT = os.path.join(RAW, 'Dumper7_allassets',
                       '5.6.1-0+++Sparta-Depot+Day1+CL92086-MortalShell2', 'CppSDK', 'SDK')

ENUM_START = re.compile(r'^enum class (\w+)')
ENUM_MEMBER = re.compile(r'^\s+(\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+),?')
STRUCT_START = re.compile(r'^struct (\w+)')
FIELD = re.compile(r'^\s+(.+?)\s+(\w+);\s+//\s+(0x[0-9A-Fa-f]+)\((0x[0-9A-Fa-f]+)\)(.*)$')


def d7_files(kind):
    if not os.path.isdir(D7_ROOT):
        return []
    pat = re.compile(r'_%s\.(hpp|cpp)$' % kind)
    out = []
    for n in sorted(os.listdir(D7_ROOT)):
        if pat.search(n):
            out.append(os.path.join(D7_ROOT, n))
    return out


def parse_enums(files):
    rows = []
    for f in files:
        mod = os.path.basename(f)[:-len('_structs.hpp')]
        cur = None
        with open(f, 'r', encoding='utf-8', errors='replace') as fh:
            for line in fh:
                m = ENUM_START.match(line)
                if m:
                    cur = m.group(1)
                    continue
                if cur:
                    if line.startswith('};'):
                        cur = None
                        continue
                    mm = ENUM_MEMBER.match(line)
                    if mm:
                        rows.append((mod, cur, mm.group(1), mm.group(2)))
    return rows


def parse_structs(files):
    rows = []
    for f in files:
        mod = os.path.basename(f)[:-len('_structs.hpp')]
        cur = None
        with open(f, 'r', encoding='utf-8', errors='replace') as fh:
            for line in fh:
                m = STRUCT_START.match(line)
                if m:
                    cur = m.group(1)
                    continue
                if cur:
                    if line.startswith('};'):
                        cur = None
                        continue
                    fm = FIELD.match(line)
                    if fm:
                        rows.append((mod, cur, fm.group(3), fm.group(4), fm.group(1).strip(),
                                     fm.group(2), fm.group(5).strip()))
    return rows


def parse_flags(files):
    rows = []
    head = re.compile(r'^// Function ([\w]+)\.([\w]+)\.(\w+)\s*$')
    flag = re.compile(r'^// \((.*)\)\s*$')
    for f in files:
        mod = os.path.basename(f)[:-len('_functions.cpp')]
        cur = None
        with open(f, 'r', encoding='utf-8', errors='replace') as fh:
            for line in fh:
                line = line.rstrip('\n')
                m = head.match(line)
                if m:
                    cur = (mod, m.group(2), m.group(3), '')
                    rows.append(cur)
                    continue
                if cur is not None:
                    fm = flag.match(line)
                    if fm:
                        rows[-1] = (cur[0], cur[1], cur[2], fm.group(1))
                        cur = None
    return rows


# ----------------------------------------------------------------------------- emit helpers

def write_tsv(name, header, rows):
    os.makedirs(MINE, exist_ok=True)
    p = os.path.join(MINE, name)
    with open(p, 'w', encoding='utf-8', newline='\n') as fh:
        fh.write('\t'.join(header) + '\n')
        for r in rows:
            fh.write('\t'.join('' if v is None else str(v).replace('\t', ' ') for v in r) + '\n')
    return p, len(rows)


def cmd_tree(args):
    c = db(args.db)
    for cls, addr, sup in chain(c, args.path):
        print('=== %s  [%s]  super=%s' % (cls, addr, sup))
        for m in own_members(c, addr):
            nm = m['path'].rsplit(':', 1)[-1] if ':' in m['path'] else m['path']
            if m['type'] == 'Function':
                ps = params_of(c, resolve_addr(c, m['path']))
                print('  FUN  %s(%s)' % (nm, ', '.join('%s %s' % (p[1], p[0]) for p in ps)))
            else:
                print('  %-9s %s : %s' % (hx(m['off']), nm, pretty_ref(m)))


def cmd_show(args):
    c = db(args.db)
    r = find_obj(c, args.path)
    if not r:
        sys.exit('not found: ' + args.path)
    print('=== %s  [%s]  super=%s' % (args.path, r['addr'], resolve(c, r['super'])))
    for m in own_members(c, r['addr']):
        nm = m['path'].rsplit(':', 1)[-1] if ':' in m['path'] else m['path']
        if m['type'] == 'Function':
            ps = params_of(c, resolve_addr(c, m['path']))
            print('  FUN  %s(%s)' % (nm, ', '.join('%s %s' % (p[1], p[0]) for p in ps)))
        else:
            print('  %-9s %s : %s' % (hx(m['off']), nm, pretty_ref(m)))


def cmd_cls(args):
    c = db(args.db)
    rx = re.compile(args.regex)
    q = """SELECT o.type,o.path,s.path AS sup FROM obj o LEFT JOIN obj s ON s.addr=UPPER(o.super)
           WHERE o.type IN (%s) ORDER BY o.path""" % ','.join('?' * len(CLASS_TYPES))
    for r in c.execute(q, CLASS_TYPES):
        if rx.search(r['path']):
            print('%s\t%s\t%s' % (r['type'], r['path'], r['sup'] or ''))


def cmd_flat(args):
    c = db(args.db)
    rx = re.compile(args.regex)
    q = """SELECT path FROM obj WHERE type IN (%s) ORDER BY path""" % ','.join('?' * len(CLASS_TYPES))
    for r in c.execute(q, CLASS_TYPES):
        if rx.search(r['path']):
            print('### %s' % r['path'])
            for decl, nm, t, off, line in flatten(c, r['path']):
                print('%s\t%s\t%s\t%s\t%s' % (decl, nm, t, off, line))


def cmd_fun(args):
    c = db(args.db)
    rx = re.compile(args.regex)
    q = """SELECT path FROM obj WHERE type IN (%s) ORDER BY path""" % ','.join('?' * len(CLASS_TYPES))
    for r in c.execute(q, CLASS_TYPES):
        if not rx.search(r['path']):
            continue
        print('### %s' % r['path'])
        for decl, nm, line, ps in funcs(c, r['path']):
            print('%s\t%s\t%s\t%s' % (decl, nm, line, '; '.join('%s |%s' % (p[1], p[0]) for p in ps)))


def cmd_enums(args):
    files = [a for a in args.files] if args.files else d7_files('structs')
    rows = parse_enums(files)
    p, n = write_tsv('enums.tsv', ('module', 'enum', 'member', 'value'), rows)
    print('%s : %d rows' % (p, n))


def cmd_structs(args):
    files = [a for a in args.files] if args.files else d7_files('structs')
    rows = parse_structs(files)
    p, n = write_tsv('structs.tsv', ('module', 'struct', 'offset', 'size', 'type', 'field', 'flags'), rows)
    print('%s : %d rows' % (p, n))


def cmd_flags(args):
    files = [a for a in args.files] if args.files else d7_files('functions')
    rows = parse_flags(files)
    p, n = write_tsv('funcflags.tsv', ('module', 'class', 'function', 'flags'), rows)
    print('%s : %d rows' % (p, n))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--db', default='index.sqlite3')
    sub = ap.add_subparsers(dest='cmd', required=True)

    p = sub.add_parser('tree'); p.add_argument('path'); p.set_defaults(fn=cmd_tree)
    p = sub.add_parser('show'); p.add_argument('path'); p.set_defaults(fn=cmd_show)
    p = sub.add_parser('cls'); p.add_argument('regex'); p.set_defaults(fn=cmd_cls)
    p = sub.add_parser('flat'); p.add_argument('regex'); p.set_defaults(fn=cmd_flat)
    p = sub.add_parser('fun'); p.add_argument('regex'); p.set_defaults(fn=cmd_fun)
    p = sub.add_parser('enums'); p.add_argument('files', nargs='*'); p.set_defaults(fn=cmd_enums)
    p = sub.add_parser('structs'); p.add_argument('files', nargs='*'); p.set_defaults(fn=cmd_structs)
    p = sub.add_parser('flags'); p.add_argument('files', nargs='*'); p.set_defaults(fn=cmd_flags)
    p = sub.add_parser('gen'); p.set_defaults(fn=None)

    args = ap.parse_args()
    if args.cmd == 'gen':
        import mine_gen
        mine_gen.main()
        return
    args.fn(args)


if __name__ == '__main__':
    sys.path.insert(0, HERE)
    main()
