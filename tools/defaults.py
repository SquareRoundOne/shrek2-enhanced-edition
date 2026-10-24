"""Parse class default properties (UE2 tags) from the tail of a Class export."""
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upkg import Package, Reader
TYPES = {1:'byte',2:'int',3:'bool',4:'float',5:'object',6:'name',7:'string',8:'class',9:'array',10:'struct',11:'vector',12:'rotator',13:'str',14:'map',15:'fixedarray'}

def parse_tags(pkg, pos, end):
    r = Reader(pkg.data, pos); out = []
    while r.p < end:
        ni = r.ci()
        if ni < 0 or ni >= len(pkg.names): return None
        nm = pkg.names[ni]
        if nm == 'None': return out if r.p == end else None
        info = r.u8(); typ = info & 0x0f; sz = (info >> 4) & 7; arr = info & 0x80
        sname = None
        if typ == 10: sname = pkg.names[r.ci()]
        size = {0:1,1:2,2:4,3:12,4:16}.get(sz)
        if sz == 5: size = r.u8()
        elif sz == 6: size = r.u16()
        elif sz == 7: size = r.u32()
        idx = 0
        if arr and typ != 3:
            b = r.u8()
            if b & 0x80:
                if b & 0x40: idx = ((b & 0x3f) << 24) | (r.u8() << 16) | r.u16()
                else: idx = ((b & 0x7f) << 8) | r.u8()
            else: idx = b
        raw = pkg.data[r.p:r.p+size]; r.p += size
        if typ == 4: val = struct.unpack('<f', raw)[0]
        elif typ == 2: val = struct.unpack('<i', raw)[0]
        elif typ == 3: val = bool(arr)
        elif typ == 1: val = raw[0] if raw else 0
        elif typ in (5, 8): val = pkg.fullname(Reader(raw).ci())
        elif typ == 6: val = pkg.names[Reader(raw).ci()]
        elif typ == 13: val = raw[1:].rstrip(b'\0').decode('latin-1', 'replace')
        else: val = raw.hex()
        out.append((nm, idx, TYPES.get(typ, typ), sname, val))
    return None

def class_defaults(pkg, clsname):
    e = [x for x in pkg.exports if x['cls'] == 0 and x['name'] == clsname][0]
    start, end = e['offset'], e['offset'] + e['size']
    cands = []
    for p in range(end - 2, start, -1):
        try:
            res = parse_tags(pkg, p, end)
        except Exception:
            continue
        if res is not None:
            cands.append(res)
    # longest valid tag list that ends exactly at the export end
    return max(cands, key=len) if cands else None

if __name__ == '__main__':
    pkg = Package(sys.argv[1])
    for c in sys.argv[2].split(','):
        d = class_defaults(pkg, c)
        print('==', c)
        for t in d or []: print('  ', t)
