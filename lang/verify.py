import sys,os,hashlib
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from texlib import *
S=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..')
G=sys.argv[1]; mode=sys.argv[2]
PK={'10_Castle_Siege_Tex':'_0_Castle_Siege_Tex','1_Shreks_Swamp_Tex':'__Shreks_Swamp_Tex','3_The_Hunt_Tex':'__The_Hunt_Tex','4_FGM_PIB_Tex':'__FGM_PIB_Tex','6_Hamlet_Tex':'__Hamlet_Tex','EnvAnimsTx':'EnvAnimsTx','FGM_Potion_Menu':'FGM_Potion_Menu','SH_Hud':'SH_Hud','SH_Menu':'SH_Menu','Shrek2_Univ_Tex':'Shrek2_Univ_Tex','storybookanimTX':'storybookanimTX'}
bad=0
for ru,en in PK.items():
    cur=f'{G}/Textures/{ru}.utx'; org=f'{S}/lt/orig/{ru}.utx'
    if mode=='ru':
        same=open(cur,'rb').read()==open(org,'rb').read()
        if not same: print('NOT IDENTICAL',ru); bad+=1
        continue
    C=Package(cur); O=Package(org); A=Package(f'{S}/en/raw/{en}.utx')
    ea={A.fullname(e['idx']):e for e in A.exports if A.classname(e)=='Texture'}
    n=0
    for e,eo in zip(C.exports,O.exports):
        k=C.fullname(e['idx']); assert k==O.fullname(eo['idx'])
        cb=C.data[e['offset']:e['offset']+e['size']]; ob=O.data[eo['offset']:eo['offset']+eo['size']]
        if cb==ob: continue
        assert C.classname(e)=='Texture',k
        p1,b1,_,t1=texbody(C,e); p2,b2,_,t2=texbody(A,ea[k])
        if b1!=b2 or p1!=p2: print('MISMATCH',ru,k); bad+=1
        # skip offsets must be absolute & correct (texbody asserts)
        n+=1
    print(ru,'patched',n)
print('bad',bad)
