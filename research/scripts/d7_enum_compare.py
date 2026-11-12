import re, json
from pathlib import Path
import sys
d7 = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(r'G:\Freespace\shell_coop\research\raw\Dumper7\SDK\Sparta_structs.hpp')
ue4 = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(r'G:\Freespace\shell_coop\research\raw\CXXHeaderDump\Sparta_enums.hpp')

def parse(path, d7style):
    text = path.read_text(encoding='utf-8', errors='replace')
    enums = {}
    cur = None
    if d7style:
        for line in text.splitlines():
            m = re.match(r'enum class (\w+)', line.strip())
            if m: cur = m.group(1); enums[cur] = {}
            else:
                m = re.match(r'(\w+)\s*=\s*(-?0x[0-9A-Fa-f]+|-?\d+)', line.strip())
                if m and cur: enums[cur][m.group(1)] = int(m.group(2), 0)
                elif line.strip().startswith('};'): cur = None
    else:
        for line in text.splitlines():
            m = re.match(r'enum class (\w+)', line.strip())
            if m: cur = m.group(1); enums[cur] = {}
            else:
                m = re.match(r'(\w+)\s*=\s*(-?0x[0-9A-Fa-f]+|-?\d+)', line.strip())
                if m and cur: enums[cur][m.group(1)] = int(m.group(2), 0)
                elif line.strip().startswith('};'): cur = None
    return enums

a = parse(d7, True); b = parse(ue4, False)
print('d7 enums', len(a), 'ue4ss enums', len(b), 'shared', len(set(a) & set(b)))
diff, same, only = [], 0, []
for name in sorted(set(a) & set(b)):
    if a[name] == b[name]: same += 1
    else:
        diff.append((name, {k: (a[name].get(k), b[name].get(k)) for k in set(a[name]) | set(b[name]) if a[name].get(k) != b[name].get(k)}))
print('identical enumerator maps:', same)
print('differing enums:', len(diff))
for name, d in diff[:6]: print('  ', name, list(d.items())[:4])
print('only in ue4ss:', sorted(set(b) - set(a))[:8])
json.dump({'d7': len(a), 'ue4ss': len(b), 'shared': len(set(a)&set(b)), 'identical': same, 'diff': {k: v for k, v in diff[:20]}}, open((sys.argv[3] if len(sys.argv) > 3 else r'G:\Freespace\shell_coop\research\slices\d7-enum-compare.json'),'w'), indent=1)
