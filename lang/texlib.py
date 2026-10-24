import sys,os,hashlib,struct
sys.path.insert(0,os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','tools'))
from upkg import Package, Reader
def props(P,off):
    """returns list of (name, typ, info, valpos, size, arrbit) and pos after None"""
    r=Reader(P.data,off); out=[]
    while True:
        ts=r.p; nm=P.names[r.ci()]
        if nm=='None': return out,r.p
        info=r.u8(); typ=info&15; sz=(info>>4)&7; arr=bool(info&0x80)
        if typ==10: r.ci()
        size={0:1,1:2,2:4,3:12,4:16}.get(sz)
        if sz==5: size=r.u8()
        elif sz==6: size=r.u16()
        elif sz==7: size=r.u32()
        if typ==3: out.append((nm,typ,info,r.p,0,arr,ts)); continue
        if arr:
            b=r.u8()
            if b&0x80:
                if b&0x40: r.u8(); r.u16()
                else: r.u8()
        out.append((nm,typ,info,r.p,size,arr,ts)); r.p+=size
def mips(P,pos,end):
    """parse mip section at pos; returns (normalized blob, [(skippos_rel, target_rel)], endpos)"""
    d=P.data; r=Reader(d,pos); n=r.ci(); skips=[]
    for i in range(n):
        sp=r.p; sk=r.i32(); cnt=r.ci(); r.p+=cnt
        assert sk==r.p,(sk,r.p)
        skips.append((sp-pos,r.p-pos))
        r.i32(); r.i32(); r.u8(); r.u8()
    blob=bytearray(d[pos:r.p])
    for s,t in skips: blob[s:s+4]=b'\0\0\0\0'
    return bytes(blob),skips,r.p
def texbody(P,e):
    pr,pe=props(P,e['offset'])
    blob,skips,me=mips(P,pe,e['offset']+e['size'])
    tail=P.data[me:e['offset']+e['size']]
    pv={}
    for nm,typ,info,vp,size,arr,ts in pr:
        if typ==3: pv[nm]=arr
        elif typ==1: pv[nm]=P.data[vp]
        elif typ==2: pv[nm]=struct.unpack_from('<i',P.data,vp)[0]
        else: pv[nm]=('t%d'%typ,P.data[vp:vp+size].hex())
    return pv,blob,skips,tail
