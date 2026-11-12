#!/usr/bin/env python3
"""Compact class dump for planning in-game probes: properties and function signatures
of one or more classes, with Blueprint construction-script noise filtered out.

Usage:
  python clsinfo.py <db> <class-path-substring> [<more substrings>...] [--all]

Filtered (noise) member names:
  *_GEN_VARIABLE, SimpleConstructionScript*, *SCS_Node*, InheritableComponentHandler,
  UberGraphFrame, ExecuteUbergraph_*, Temp_*, CallFunc_*, K2Node_*, DefaultSceneRoot*
Pass --all to keep everything.
"""
import re
import sqlite3
import sys

NOISE = re.compile(r'(_GEN_VARIABLE$|^SimpleConstructionScript|SCS_Node|InheritableComponentHandler|'
                   r'^UberGraphFrame$|^ExecuteUbergraph_|^Temp_|^CallFunc_|^K2Node_|^DefaultSceneRoot)')


def main():
    db = sys.argv[1]
    args = [a for a in sys.argv[2:] if a != '--all']
    keep_all = '--all' in sys.argv
    con = sqlite3.connect(db)

    def resolve(ref):
        if not ref:
            return ''
        r = con.execute('SELECT path FROM obj WHERE addr=?', (ref.upper(),)).fetchone()
        return r[0] if r else ref

    for needle in args:
        rows = con.execute(
            "SELECT addr, type, path, super FROM cls WHERE path LIKE ? AND path NOT LIKE '%:%' ORDER BY path",
            ('%' + needle + '%',)).fetchall()
        if not rows:
            print(f'## {needle}: NO CLASS MATCH')
            continue
        for addr, typ, path, superaddr in rows:
            print(f'## {path}  [{typ}]')
            print(f'   parent: {resolve(superaddr)}')
            for otype, opath, ooff, oref in con.execute(
                    'SELECT type, path, off, ref FROM obj WHERE owner=? ORDER BY line', (addr,)):
                name = opath.rsplit(':', 1)[-1]
                if not keep_all and NOISE.search(name):
                    continue
                off = f' [o:{ooff:#x}]' if ooff is not None else ''
                if otype in ('Function', 'DelegateFunction'):
                    ps = con.execute('SELECT type, path, ref FROM obj WHERE owner=? ORDER BY line',
                                     (con.execute('SELECT addr FROM obj WHERE path=? AND type=?',
                                                  (opath, otype)).fetchone()[0],)).fetchall()
                    sig = []
                    for ptype, ppath, pref in ps:
                        pn = ppath.rsplit(':', 1)[-1]
                        if not keep_all and NOISE.search(pn):
                            continue
                        sig.append(f'{pn}: {ptype}{(" of " + resolve(pref)) if pref else ""}')
                    print(f'   F {name}({", ".join(sig)})')
                else:
                    print(f'   P {name} : {otype}{(" of " + resolve(oref)) if oref else ""}{off}')
            print()


if __name__ == '__main__':
    main()
