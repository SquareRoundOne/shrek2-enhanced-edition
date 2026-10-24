"""Minimal Unreal Engine 2 package (.u) reader."""
import struct, sys


class Reader:
    def __init__(self, data, pos=0):
        self.d = data
        self.p = pos

    def u8(self):
        v = self.d[self.p]; self.p += 1; return v

    def u16(self):
        v = struct.unpack_from('<H', self.d, self.p)[0]; self.p += 2; return v

    def u32(self):
        v = struct.unpack_from('<I', self.d, self.p)[0]; self.p += 4; return v

    def i32(self):
        v = struct.unpack_from('<i', self.d, self.p)[0]; self.p += 4; return v

    def f32(self):
        v = struct.unpack_from('<f', self.d, self.p)[0]; self.p += 4; return v

    def ci(self):
        """UE compact index."""
        b0 = self.u8()
        v = b0 & 0x3F
        neg = b0 & 0x80
        if b0 & 0x40:
            shift = 6
            for _ in range(4):
                b = self.u8()
                v |= (b & 0x7F) << shift
                shift += 7
                if not b & 0x80:
                    break
        return -v if neg else v


class Package:
    def __init__(self, path):
        self.path = path
        self.data = open(path, 'rb').read()
        r = Reader(self.data)
        tag = r.u32()
        assert tag == 0x9E2A83C1, hex(tag)
        self.version = r.u16()
        self.licensee = r.u16()
        self.flags = r.u32()
        nc, no, ec, eo, ic, io = (r.u32() for _ in range(6))
        self.names = []
        r.p = no
        for _ in range(nc):
            ln = r.ci()
            s = self.data[r.p:r.p + ln].rstrip(b'\0').decode('latin-1')
            r.p += ln
            r.u32()
            self.names.append(s)
        self.imports = []
        r.p = io
        for _ in range(ic):
            cp = r.ci(); cn = r.ci(); pkg = r.i32(); on = r.ci()
            self.imports.append(dict(class_pkg=self.names[cp], class_name=self.names[cn],
                                     package=pkg, name=self.names[on]))
        self.exports = []
        r.p = eo
        for i in range(ec):
            start = r.p
            cls = r.ci(); sup = r.ci(); pkg = r.i32(); on = r.ci(); fl = r.u32()
            size = r.ci(); off = r.ci() if size > 0 else 0
            self.exports.append(dict(idx=i + 1, cls=cls, super=sup, package=pkg,
                                     name=self.names[on], flags=fl, size=size,
                                     offset=off, table_pos=start))

    def objname(self, ref):
        if ref == 0:
            return 'None'
        if ref > 0:
            return self.exports[ref - 1]['name']
        return self.imports[-ref - 1]['name']

    def fullname(self, ref):
        parts = []
        while ref:
            if ref > 0:
                e = self.exports[ref - 1]; parts.append(e['name']); ref = e['package']
            else:
                im = self.imports[-ref - 1]; parts.append(im['name']); ref = im['package']
        return '.'.join(reversed(parts))

    def classname(self, e):
        return self.objname(e['cls']) if e['cls'] else 'Class'


if __name__ == '__main__':
    p = Package(sys.argv[1])
    print('version', p.version, 'licensee', p.licensee, 'names', len(p.names),
          'exports', len(p.exports), 'imports', len(p.imports))
