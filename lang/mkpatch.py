# Build the English texture overlay: for every Texture whose pixels differ between the English
# disc (en/raw) and the Russian install (s2), store the English property block (names as strings)
# and the English mip section. The launcher applies it to the player's own packages.
import sys,os,struct
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from texlib import *
S=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..')
PK={'10_Castle_Siege_Tex':'_0_Castle_Siege_Tex','1_Shreks_Swamp_Tex':'__Shreks_Swamp_Tex','3_The_Hunt_Tex':'__The_Hunt_Tex','4_FGM_PIB_Tex':'__FGM_PIB_Tex','6_Hamlet_Tex':'__Hamlet_Tex','EnvAnimsTx':'EnvAnimsTx','FGM_Potion_Menu':'FGM_Potion_Menu','SH_Hud':'SH_Hud','SH_Menu':'SH_Menu','Shrek2_Univ_Tex':'Shrek2_Univ_Tex','storybookanimTX':'storybookanimTX'}
def s(x): b=x.encode(); return struct.pack('<H',len(b))+b
def ciparse(d,p):
    r=Reader(d,p); v=r.ci(); return v,r.p
out=bytearray(b'SHRLTEX1'); pk=[]
total=0
for ru,en in PK.items():
    A=Package(f'{S}/en/raw/{en}.utx'); B=Package(f'{S}/s2/Shrek 2/Textures/{ru}.utx')
    ea={A.fullname(e['idx']):e for e in A.exports if A.classname(e)=='Texture'}
    eb={B.fullname(e['idx']):e for e in B.exports if B.classname(e)=='Texture'}
    ents=[]
    for k in sorted(set(ea)&set(eb)):
        e=ea[k]
        pa,ba,sk,ta=texbody(A,e); pb,bb,_,_=texbody(B,eb[k])
        if ba==bb: continue
        assert not ta
        pr,pe=props(A,e['offset'])
        rec=bytearray(s(k)+struct.pack('<I',len(pr)))
        for nm,typ,info,vp,size,arr,ts in pr:
            r=Reader(A.data,ts); r.ci(); r.u8()
            sn=''
            if typ==10: sn=A.names[r.ci()]
            ex=A.data[r.p:(vp if vp is not None else r.p)]
            rec+=s(nm)+bytes([info])+s(sn)+struct.pack('<H',len(ex))+ex
            if typ==3: rec+=b'\0'+struct.pack('<I',0)
            elif typ==5:
                v,_=ciparse(A.data,vp); rec+=b'\1'+s(A.fullname(v) if v else '')
            elif typ==4:
                v,_=ciparse(A.data,vp); rec+=b'\2'+s(A.names[v])
            else:
                rec+=b'\0'+struct.pack('<I',size)+A.data[vp:vp+size]
            if typ in (5,4): print('  ref prop',ru,k,nm,typ)
        rec+=struct.pack('<I',len(ba))+ba+struct.pack('<I',len(sk))
        for p,t in sk: rec+=struct.pack('<II',p,t)
        ents.append(bytes(rec))
    out+=s('Textures/'+ru+'.utx')+struct.pack('<I',len(ents))
    for r_ in ents: out+=r_
    total+=len(ents); print(ru,len(ents))
out[8:8]=b''
dst=sys.argv[1]
open(dst,'wb').write(bytes(out[:8])+struct.pack('<I',len(PK))+bytes(out[8:]))
print('total',total,'bytes',len(out))
