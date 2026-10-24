import sys,re,os; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from pe import PE
class Asm:
    def __init__(s, dll, asm):
        s.p=PE(dll); s.imp=s.p.imports(); s.ex=s.p.exports()
        s.L=open(asm).read().splitlines(); s.idx={}
        for i,l in enumerate(s.L):
            m=re.match(r'^([0-9a-f]{8}):',l)
            if m: s.idx[int(m.group(1),16)]=i
    def follow(s,a):
        if a not in s.idx: return a
        l=s.L[s.idx[a]]; m=re.search(r'\tjmp\s+0x([0-9a-f]+)',l); return int(m.group(1),16) if m else a
    def show(s,a,n=200,stop=True):
        i=s.idx[a]; out=[]
        for l in s.L[i:i+n]:
            m=re.search(r'ds:(0x[0-9a-f]+)',l)
            x=('   ; '+s.imp[int(m.group(1),16)]) if m and int(m.group(1),16) in s.imp else ''
            out.append(l.split('\t')[0]+' '+l.split('\t')[-1][:80]+x)
            if stop and re.search(r'\tret',l): break
        print('\n'.join(out))
if __name__=='__main__':
    a=Asm(sys.argv[1],sys.argv[2])
    for nm in sys.argv[3:]:
        addr=a.follow(a.ex[nm]); print('==',nm,hex(addr)); a.show(addr)
