"""Property edits in UE2 map packages (.unr), append-only.

The original bytes stay where they are (models, terrain and static mesh data carry absolute
offsets); a changed actor gets a new body appended at the end, and the name, import and export
tables are written again after it (the header points at the copies).

    m = MapPatch(path)
    m.set(export_name, prop, value, kind)   kind: bool|byte|int|float|color(r,g,b,a)
    m.save(out)
"""
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upkg import Package, Reader
from uprops import read_props, T_BOOL, T_BYTE, T_INT, T_FLOAT, T_STRUCT
from ubc import enc_ci


def _tag(name_idx, typ, size_code, struct_idx=None, arr=False):
    if typ == T_BOOL:   # bool: size 0 (written as size code 5 + a zero byte), the value is the array bit
        return enc_ci(name_idx) + bytes([T_BOOL | 0x50 | (0x80 if arr else 0), 0])
    b = enc_ci(name_idx) + bytes([typ | (size_code << 4) | (0x80 if arr else 0)])
    if struct_idx is not None:
        b += enc_ci(struct_idx)
    return b


class MapPatch:
    def __init__(self, path):
        self.pkg = Package(path)
        self.names = list(self.pkg.names)
        self.edits = {}      # export idx -> {prop: (kind, value)}

    def find(self, name):
        for e in self.pkg.exports:
            if e['name'] == name:
                return e
        raise KeyError(name)

    def name_idx(self, s):
        if s not in self.names:
            self.names.append(s)
        return self.names.index(s)

    def set(self, export, prop, value, kind):
        e = self.find(export) if isinstance(export, str) else export
        self.edits.setdefault(e['idx'], {})[prop] = (kind, value)

    def _value(self, kind, v):
        if kind == 'bool': return T_BOOL, 0, b'', bool(v)
        if kind == 'byte': return T_BYTE, 0, bytes([v]), False
        if kind == 'int': return T_INT, 2, struct.pack('<i', v), False
        if kind == 'float': return T_FLOAT, 2, struct.pack('<f', v), False
        if kind == 'color':   # r, g, b, a -> stored B G R A
            r, g, b, a = v; return T_STRUCT, 2, bytes([b, g, r, a]), False
        raise ValueError(kind)

    def _body(self, e, edits):
        pkg = self.pkg; d = pkg.data
        props, end = read_props(pkg, e)          # end: just after the "None" name
        start = e['offset']
        first = props[0].tagpos if props else None
        if first is None:   # no properties: the stack frame (if any) ends where "None" begins
            r = Reader(d, start)
            if e['flags'] & 0x02000000:
                node = r.ci(); r.ci(); r.p += 8; r.p += 4
                if node: r.ci()
            first = r.p
        head = d[start:first]
        out = bytearray(head)
        done = set()
        for p in props:
            tagend = p.off   # value bytes start here (bool: no value)
            if p.name in edits and p.idx == 0:
                kind, v = edits[p.name]; done.add(p.name)
                typ, sc, val, barr = self._value(kind, v)
                if kind == 'bool':
                    out += _tag(self.name_idx(p.name), T_BOOL, 0, arr=barr)
                elif kind == 'color':
                    out += _tag(self.name_idx(p.name), T_STRUCT, 2, self.name_idx('Color')) + val
                else:
                    out += _tag(self.name_idx(p.name), typ, sc) + val
            else:
                out += d[p.tagpos:p.off + p.size]
        for name, (kind, v) in edits.items():
            if name in done: continue
            typ, sc, val, barr = self._value(kind, v)
            if kind == 'bool':
                if not barr: continue          # false is the default
                out += _tag(self.name_idx(name), T_BOOL, 0, arr=True)
            elif kind == 'color':
                out += _tag(self.name_idx(name), T_STRUCT, 2, self.name_idx('Color')) + val
            else:
                out += _tag(self.name_idx(name), typ, sc) + val
        # "None" and whatever follows the properties
        none_pos = end - len(enc_ci(self.pkg.names.index('None')))
        out += enc_ci(self.name_idx('None'))
        out += d[end:start + e['size']]
        return bytes(out)

    def save(self, path):
        pkg = self.pkg; d = pkg.data
        h = list(struct.unpack_from('<IHHI6I', d, 0))
        out = bytearray(d)
        exps = [dict(e) for e in pkg.exports]
        for e in exps:
            if e['idx'] in self.edits:
                body = self._body(e, self.edits[e['idx']])
                e['offset'] = len(out); e['size'] = len(body)
                out += body
        # names (flags kept; new names get the usual object flags)
        flags = []
        r = Reader(d, h[5])
        for _ in range(h[4]):
            ln = r.ci(); r.p += ln; flags.append(r.u32())
        no = len(out)
        for i, n in enumerate(self.names):
            b = n.encode('latin-1') + b'\0'
            out += enc_ci(len(b)) + b + struct.pack('<I', flags[i] if i < len(flags) else 0x00070010)
        io = len(out)
        for im in pkg.imports:
            out += enc_ci(self.names.index(im['class_pkg'])) + enc_ci(self.names.index(im['class_name']))
            out += struct.pack('<i', im['package']) + enc_ci(self.names.index(im['name']))
        eo = len(out)
        for e in exps:
            out += enc_ci(e['cls']) + enc_ci(e['super']) + struct.pack('<i', e['package'])
            out += enc_ci(self.names.index(e['name'])) + struct.pack('<I', e['flags']) + enc_ci(e['size'])
            if e['size'] > 0:
                out += enc_ci(e['offset'])
        h[4] = len(self.names); h[5] = no; h[7] = eo; h[8] = len(pkg.imports); h[9] = io
        struct.pack_into('<IHHI6I', out, 0, *h)
        # generations (after the GUID): the last one carries the export and name counts
        gc = struct.unpack_from('<i', out, 52)[0]
        if 0 < gc < 64:
            struct.pack_into('<ii', out, 56 + 8 * (gc - 1), len(exps), len(self.names))
        open(path, 'wb').write(out)
