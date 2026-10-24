import sys,re,difflib
def load(p):
    d={}
    for blk in open(p).read().split('\n== '):
        n=blk.split('\n',1)[0].lstrip('= ').strip(); b=blk.split('\n',1)[1] if '\n' in blk else ''
        b=re.sub(r'^  [0-9a-f]{4}: ','',b,flags=re.M); b=re.sub(r'->0x[0-9a-f]+','->X',b)
        d[n]=b
    return d
a=load(sys.argv[1]); b=load(sys.argv[2])
print('only EN:',sorted(set(a)-set(b))); print('only RU:',sorted(set(b)-set(a)))
ch=[n for n in sorted(set(a)&set(b)) if a[n]!=b[n]]
print('changed:',len(ch))
for n in ch:
    print('###',n)
    for l in difflib.unified_diff(a[n].splitlines(),b[n].splitlines(),lineterm='',n=1):
        if l.startswith(('---','+++')): continue
        print('   ',l[:220])
