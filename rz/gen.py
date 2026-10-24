import re,sys
H=open(sys.argv[1]).read()
def methods(iface):
    m=re.search(r'DECLARE_INTERFACE_\('+iface+r',.*?\n\{(.*?)\n\};',H,re.S)
    body=m.group(1)
    body=re.sub(r'/\*.*?\*/','',body,flags=re.S)
    out=[]
    for st in re.findall(r'STDMETHOD(?:_\(([^,]+),\s*(\w+)\s*\)|\((\w+)\))\(THIS_?(.*?)\)\s*PURE;',body,re.S):
        ret=st[0].strip() or 'HRESULT'; name=(st[1] or st[2]).strip(); params=' '.join(st[3].split()).strip()
        out.append((ret,name,params))
    return out
def argnames(params):
    if not params: return ''
    names=[]
    depth=0; cur=''
    parts=[]
    for ch in params:
        if ch==',' : parts.append(cur); cur=''
        else: cur+=ch
    parts.append(cur)
    for p in parts:
        p=p.strip(); n=re.findall(r'(\w+)\s*(?:\[\])?$',p)[0]; names.append(n)
    return ', '.join(names)
skip=set(sys.argv[4].split(','))
cls=sys.argv[3]; iface=sys.argv[2]
for ret,name,params in methods(iface):
    if name in skip or name in('QueryInterface','AddRef','Release'): continue
    print(f'    {ret} STDMETHODCALLTYPE {name}({params}) override {{ return r->{name}({argnames(params)}); }}')
