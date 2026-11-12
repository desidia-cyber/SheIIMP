import os, re, sys
HERE=os.path.abspath('scripts'); sys.path.insert(0,HERE)
import mine
CXX=os.path.join('raw','CXXHeaderDump')
start=re.compile(r'^struct (\w+)\s*$')
start2=re.compile(r'^struct (\w+)\s*:\s*public\s+\S+\s*$')
field=re.compile(r'^\s{4}(.+?)\s+(\w+);\s+//\s+(0x[0-9A-Fa-f]+)\s+\(size:\s+(0x[0-9A-Fa-f]+)\)')
rows=[]
for n in sorted(os.listdir(CXX)):
    if not n.endswith('.hpp'): continue
    mod=n[:-4]; cur=None
    with open(os.path.join(CXX,n),encoding='utf-8',errors='replace') as fh:
        for line in fh:
            line=line.rstrip('\n')
            m=start.match(line) or start2.match(line)
            if m: cur=m.group(1); continue
            if cur:
                if line.startswith('};'): cur=None; continue
                f=field.match(line)
                if f: rows.append((mod,cur,f.group(3),f.group(4),f.group(1).strip(),f.group(2)))
p,n=mine.write_tsv('structs-ue4ss.tsv',('module','struct','field_offset','field_size','field_type','field'),rows)
print(p,n)
