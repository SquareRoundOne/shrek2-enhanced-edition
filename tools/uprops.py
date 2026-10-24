"""UE2 tagged properties: read the property block of an export (maps, actors).

props(pkg, export) -> list of Prop(name, typ, idx, struct, off, size, value)
  off/size: the value bytes inside pkg.data (for in-place patching)
"""
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upkg import Package, Reader

T_BYTE, T_INT, T_BOOL, T_FLOAT, T_OBJ, T_NAME, T_STR, T_CLASS, T_ARRAY, T_STRUCT, T_VEC, T_ROT, T_STR2, T_MAP, T_FIX = range(1, 16)


class Prop:
    __slots__ = ('name', 'typ', 'idx', 'struct', 'off', 'size', 'value', 'tagpos', 'info')

    def __repr__(self):
        s = self.name + (f'[{self.idx}]' if self.idx else '')
        return f'{s}={self.value!r}'


def read_props(pkg, e):
    if e['size'] <= 0:
        return [], 0
    r = Reader(pkg.data, e['offset'])
    if e['flags'] & 0x02000000:     # RF_HasStack: state frame first
        node = r.ci(); r.ci(); r.p += 8; r.p += 4
        if node: r.ci()
    out = []
    end = e['offset'] + e['size']
    while r.p < end:
        tp = r.p
        ni = r.ci()
        name = pkg.names[ni]
        if name == 'None':
            break
        info = r.u8()
        typ = info & 15; sz = (info >> 4) & 7; arr = info & 0x80
        sname = None
        if typ == T_STRUCT:
            sname = pkg.names[r.ci()]
        size = [1, 2, 4, 12, 16, 0, 0, 0][sz]
        if sz == 5: size = r.u8()
        elif sz == 6: size = r.u16()
        elif sz == 7: size = r.u32()
        idx = 0
        if arr and typ != T_BOOL:
            b = r.u8()
            if b & 0x80:
                if b & 0x40:
                    idx = ((b & 0x3f) << 24) | (r.u8() << 16) | r.u16()
                else:
                    idx = ((b & 0x7f) << 8) | r.u8()
            else:
                idx = b
        p = Prop(); p.name = name; p.typ = typ; p.idx = idx; p.struct = sname; p.off = r.p; p.size = size; p.tagpos = tp; p.info = info
        d = pkg.data[r.p:r.p + size]
        if typ == T_BOOL: p.value = bool(arr); p.size = 0
        elif typ == T_BYTE and size == 1: p.value = d[0]
        elif typ == T_INT: p.value = struct.unpack('<i', d)[0]
        elif typ == T_FLOAT: p.value = struct.unpack('<f', d)[0]
        elif typ in (T_OBJ, T_CLASS):
            p.value = pkg.fullname(Reader(d).ci())
        elif typ == T_NAME: p.value = pkg.names[Reader(d).ci()]
        elif typ == T_STRUCT and sname in ('Vector',) and size == 12: p.value = struct.unpack('<3f', d)
        elif typ == T_STRUCT and sname in ('Rotator',) and size == 12: p.value = struct.unpack('<3i', d)
        elif typ == T_STRUCT and sname == 'Color' and size == 4: p.value = tuple(d)   # B G R A
        elif typ == T_STR or typ == T_STR2:
            rr = Reader(d); n = rr.ci(); p.value = d[rr.p:rr.p + abs(n)].rstrip(b'\0').decode('latin-1', 'replace')
        else: p.value = d.hex() if size <= 32 else f'<{size} bytes>'
        r.p += size
        out.append(p)
    return out, r.p


if __name__ == '__main__':
    pkg = Package(sys.argv[1])
    want = sys.argv[2:] if len(sys.argv) > 2 else None
    for e in pkg.exports:
        cn = pkg.classname(e)
        if want and cn not in want and e['name'] not in want:
            continue
        ps, _ = read_props(pkg, e)
        print(f"{e['name']} ({cn})")
        for p in ps:
            print('   ', p)
