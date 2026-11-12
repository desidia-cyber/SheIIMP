for p in ('mine/SYNC-DATA-MINE.md','mine/README.md'):
    s=open(p,encoding='utf-8').read()
    n=s.count('791')
    s=s.replace('791','837')
    open(p,'w',encoding='utf-8',newline='\n').write(s)
    print(p,'replaced',n)
