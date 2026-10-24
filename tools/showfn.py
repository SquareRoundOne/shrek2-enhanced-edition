import sys, os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upkg import Package
from ubc import Func, Node
def show(p, n, ind=0, out=print):
    fs=[]
    for k,v in n.f:
        if k in ('expr','params'): continue
        if k=='ref': v=f'{v}:{p.fullname(v) if v else None}'
        if k=='name': v=p.names[v]
        fs.append(f'{k}={v}')
    out('  '*ind+f'{n.t:#04x} @{n.old} '+' '.join(map(str,fs)))
    for k,v in n.f:
        if k=='expr' and v is not None: show(p,v,ind+1,out)
        if k=='params':
            for x in v: show(p,x,ind+1,out)
if __name__=='__main__':
    p=Package(sys.argv[1])
    for e in p.exports:
        if p.classname(e)=='Function' and p.fullname(e['idx']).endswith(sys.argv[2]):
            print('==',p.fullname(e['idx']))
            f=Func(p,e)
            for s in f.stmts: show(p,s)
