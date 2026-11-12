import json, os, sys
sys.path.insert(0,os.path.abspath('scripts')); import mine
rows=[]; man={}
for side in ('A','B'):
    p=os.path.join('mine','wire','out_%s.txt'%side)
    raw=open(p,encoding='utf-8').read().strip()
    d=json.loads(raw)
    man[side]=(len(raw.encode('utf-8')), len(d))
    with open(os.path.join('mine','wire','out_%s.pretty.json'%side),'w',encoding='utf-8',newline='\n') as fh:
        json.dump(d,fh,indent=1,ensure_ascii=False,sort_keys=True)
    print(side,'bytes',man[side][0],'top-level keys',len(d))
    for k,v in d.items():
        t=type(v).__name__
        if isinstance(v,list):
            t='list[%d]'%len(v)
            sample=json.dumps(v[:2],ensure_ascii=False)[:120]
        elif isinstance(v,dict):
            t='dict[%d]'%len(v)
            sample=json.dumps(dict(list(v.items())[:2]),ensure_ascii=False)[:120]
        else:
            sample=repr(v)[:120]
        rows.append((side,k,t,sample))
# field sizes: measure incremental json length per key on A
d=json.loads(open(os.path.join('mine','wire','out_A.txt'),encoding='utf-8').read().strip())
sizes=sorted(((len(json.dumps({k:v}))-2,k) for k,v in d.items()),reverse=True)
mine.write_tsv('wire-fields.tsv',('side','field','type','sample'),rows)
mine.write_tsv('wire-field-sizes.tsv',('json_bytes','field'),[(n,k) for n,k in sizes])
print('biggest A fields:',sizes[:8])
print('total A json bytes (compact):',len(json.dumps(d,separators=(',',':'))))
