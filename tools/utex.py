"""UE2 Texture export reader: properties + mip chain (TLazyArray)."""
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upkg import Package, Reader

FMT = {0: 'P8', 1: 'RGBA7', 2: 'RGB16', 3: 'DXT1', 4: 'RGB8', 5: 'RGBA8', 6: 'NODATA', 7: 'DXT3', 8: 'DXT5', 9: 'L8', 10: 'G16', 11: 'RRRGGGBBB'}

def read_props(pkg, pos):
    """returns (dict name->value(raw for structs), end_pos)"""
    r = Reader(pkg.data, pos); props = {}
    while True:
        ni = r.ci(); nm = pkg.names[ni]
        if nm == 'None': return props, r.p
        info = r.u8(); typ = info & 0x0f; sz = (info >> 4) & 7; arr = info & 0x80
        if typ == 10: r.ci()
        size = {0: 1, 1: 2, 2: 4, 3: 12, 4: 16}.get(sz)
        if sz == 5: size = r.u8()
        elif sz == 6: size = r.u16()
        elif sz == 7: size = r.u32()
        if arr and typ != 3:
            b = r.u8()
            if b & 0x80:
                if b & 0x40: r.u8(); r.u16()
                else: r.u8()
        raw = pkg.data[r.p:r.p + size]; r.p += size
        if typ == 3: v = bool(arr)
        elif typ == 1: v = raw[0]
        elif typ == 2: v = struct.unpack('<i', raw)[0]
        elif typ == 4: v = struct.unpack('<f', raw)[0]
        elif typ in (5, 8): v = Reader(raw).ci()
        elif typ == 6: v = pkg.names[Reader(raw).ci()]
        else: v = raw
        props[nm] = v

def read_texture(pkg, e):
    props, p = read_props(pkg, e['offset'])
    r = Reader(pkg.data, p)
    n = r.ci(); mips = []
    for _ in range(n):
        skip = r.i32()          # absolute end offset (lazy array)
        cnt = r.ci(); data_off = r.p; r.p += cnt
        w = r.i32(); h = r.i32(); ub = r.u8(); vb = r.u8()
        mips.append(dict(off=data_off, size=cnt, w=w, h=h))
    tail = pkg.data[r.p:e['offset'] + e['size']]
    return props, mips, tail

if __name__ == '__main__':
    tot = {}; npx = 0; ntex = 0
    for path in sys.argv[1:]:
        pkg = Package(path)
        for e in pkg.exports:
            if pkg.classname(e) != 'Texture' or not e['size']: continue
            try:
                props, mips, tail = read_texture(pkg, e)
            except Exception as ex:
                print('ERR', path, pkg.fullname(e['idx']), ex); continue
            if not mips: continue
            f = FMT.get(props.get('Format', 0), props.get('Format'))
            w, h = mips[0]['w'], mips[0]['h']
            k = (f, max(w, h)); tot[k] = tot.get(k, 0) + 1
            npx += w * h; ntex += 1
    for k in sorted(tot, key=lambda k: (str(k[0]), k[1])): print(k, tot[k])
    print('textures', ntex, 'Mpix', npx / 1e6)
