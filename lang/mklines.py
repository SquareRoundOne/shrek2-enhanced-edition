# tiny package with the one English line the Russian voice pack lacks
import sys,os,struct
sys.path.insert(0,os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','tools'))
from upkg import Package,Reader
def ci(v):
    a=abs(v); b0=(0x80 if v<0 else 0)|(a&0x3f); a>>=6
    if a: b0|=0x40
    o=bytes([b0])
    while a:
        b=a&0x7f; a>>=7
        if a: b|=0x80
        o+=bytes([b])
    return o
E=Package(sys.argv[1]); out=sys.argv[2]
want=['pc_shk_ShrekEscPrison_2']
names=['None','bik','Core','Engine','Class','Package','Sound']+want
N={n.lower():i for i,n in enumerate(names)}
exps=[]; body=b''
data_start=None
hdr_len=64
# names table
nt=b''.join(ci(len(n)+1)+n.encode()+b'\0'+struct.pack('<I',0x00070010) for n in names)
pos=hdr_len+len(nt)
bodies=b''; ex=[]
for w in want:
    e=[x for x in E.exports if x['name'].lower()==w.lower()][0]
    r=Reader(E.data,e['offset']); assert E.names[r.ci()]=='None'; ft=E.names[r.ci()]; r.i32(); cnt=r.ci()
    dp=r.p; end=e['offset']+e['size']
    pre=ci(N['none'])+ci(N[ft.lower()])
    b=pre+b'\0\0\0\0'+ci(cnt)+E.data[dp:dp+cnt]
    absb=pos+len(bodies)
    b=b[:len(pre)]+struct.pack('<I',absb+len(b))+b[len(pre)+4:]
    b+=E.data[dp+cnt:end]
    ex.append((N[w.lower()],e['flags'],len(b),absb)); bodies+=b
imp_off=pos+len(bodies)
imps=ci(N['core'])+ci(N['class'])+struct.pack('<i',-2)+ci(N['sound']) + ci(N['core'])+ci(N['package'])+struct.pack('<i',0)+ci(N['engine'])
exp_off=imp_off+len(imps)
et=b''.join(ci(-1)+ci(0)+struct.pack('<i',0)+ci(n)+struct.pack('<I',fl)+ci(sz)+ci(off) for n,fl,sz,off in ex)
hdr=struct.pack('<IHHI',0x9E2A83C1,129,0,1)+struct.pack('<6I',len(names),hdr_len,len(ex),exp_off,2,imp_off)+E.data[36:52]+struct.pack('<III',1,len(ex),len(names))
assert len(hdr)==64
open(out,'wb').write(hdr+nt+bodies+imps+et)
P=Package(out); print([ (P.fullname(e['idx']),P.classname(e),e['size']) for e in P.exports], len(P.data))
