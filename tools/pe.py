import struct, re, json, sys
class PE:
    def __init__(s, path):
        f = s.f = bytearray(open(path, 'rb').read())
        pe = struct.unpack_from('<I', f, 0x3c)[0]; s.opt = opt = pe + 24
        s.base = struct.unpack_from('<I', f, opt + 28)[0]
        nsec = struct.unpack_from('<H', f, pe + 6)[0]; s.so = so = opt + struct.unpack_from('<H', f, pe + 20)[0]
        s.secs = [list(struct.unpack_from('<8sIIII', f, so + i * 40)) for i in range(nsec)]
    def r2o(s, r):
        for n, vs, va, rs, ro in s.secs:
            if va <= r < va + max(vs, rs): return r - va + ro
    def v2o(s, v): return s.r2o(v - s.base)
    def cstr(s, o): return s.f[o:s.f.index(b'\0', o)].decode('latin-1')
    def exports(s):
        o = s.r2o(struct.unpack_from('<I', s.f, s.opt + 96)[0])
        nn, af, anp, aop = struct.unpack_from('<4I', s.f, o + 24)
        out = {}
        for i in range(nn):
            name = s.cstr(s.r2o(struct.unpack_from('<I', s.f, s.r2o(anp) + 4 * i)[0]))
            ordi = struct.unpack_from('<H', s.f, s.r2o(aop) + 2 * i)[0]
            out[name] = s.base + struct.unpack_from('<I', s.f, s.r2o(af) + 4 * ordi)[0]
        return out
    def imports(s):
        d = struct.unpack_from('<I', s.f, s.opt + 104)[0]; out = {}
        while True:
            ot, td, fc, nm, ft = struct.unpack_from('<5I', s.f, s.r2o(d))
            if not nm: break
            dll = s.cstr(s.r2o(nm)); t = ot or ft; i = 0
            while True:
                v = struct.unpack_from('<I', s.f, s.r2o(t) + 4 * i)[0]
                if not v: break
                out[s.base + ft + 4 * i] = dll + '!' + (s.cstr(s.r2o(v + 2)) if not v & 0x80000000 else f'#{v & 0xffff}')
                i += 1
            d += 20
        return out
