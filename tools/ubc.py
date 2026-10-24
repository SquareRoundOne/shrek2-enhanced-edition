"""UnrealScript (UE2 v129) bytecode AST: parse -> transform -> re-emit with correct
memory offsets (jumps), disk encoding (compact indices) and ScriptSize."""
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upkg import Package, Reader


def enc_ci(v):
    neg = v < 0
    v = abs(v)
    b0 = (v & 0x3F) | (0x80 if neg else 0)
    v >>= 6
    out = bytearray()
    if v:
        b0 |= 0x40
    out.append(b0)
    while v:
        b = v & 0x7F
        v >>= 7
        if v:
            b |= 0x80
        out.append(b)
    return bytes(out)


class Node:
    """t: token byte. f: list of fields (kind, value)."""
    __slots__ = ('t', 'f', 'old', '_size')

    def __init__(self, t, f=None, old=None):
        self.t = t
        self.f = f or []
        self.old = old  # original memory offset

    def __repr__(self):
        return f'Node({self.t:#x},{self.f})'

    def exprs(self):
        for k, v in self.f:
            if k == 'expr' and v is not None:
                yield v
            elif k == 'params':
                yield from v

# field grammar
G = {
    0x00: ['ref'], 0x01: ['ref'], 0x02: ['ref'], 0x04: ['expr'], 0x05: ['vsize', 'expr'],
    0x06: ['jump'], 0x07: ['jump', 'expr'], 0x08: [], 0x09: ['u16', 'expr'], 0x0a: ['case'],
    0x0b: [], 0x0c: ['labels'], 0x0d: ['expr'], 0x0e: ['expr'], 0x0f: ['expr', 'expr'],
    0x10: ['expr', 'expr'], 0x11: ['expr', 'expr', 'expr', 'expr'],
    0x12: ['expr', 'skip', 'u8', 'expr'], 0x13: ['ref', 'expr'], 0x14: ['expr', 'expr'],
    0x15: ['expr'], 0x16: [], 0x17: [], 0x18: ['skip', 'expr'], 0x19: ['expr', 'skip', 'u8', 'expr'],
    0x1a: ['expr', 'expr'], 0x1b: ['name', 'params'], 0x1c: ['ref', 'params'], 0x1d: ['i32'],
    0x1e: ['f32'], 0x1f: ['zstr'], 0x20: ['ref'], 0x21: ['name'], 0x22: ['i32', 'i32', 'i32'],
    0x23: ['f32', 'f32', 'f32'], 0x24: ['u8'], 0x25: [], 0x26: [], 0x27: [], 0x28: [],
    0x29: ['ref'], 0x2a: [], 0x2c: ['u8'], 0x2d: ['expr'], 0x2e: ['ref', 'expr'],
    0x2f: ['expr', 'jump'], 0x30: [], 0x31: [], 0x32: ['ref', 'expr', 'expr'],
    0x33: ['ref', 'expr', 'expr'], 0x34: ['wstr'], 0x36: ['ref', 'expr'], 0x37: ['expr'],
    0x38: ['name', 'params'], 0x39: ['u8', 'expr'], 0x40: ['expr', 'expr', 'expr'],
    0x41: ['expr', 'expr', 'expr'], 0x42: ['u32', 'u32', 'u32', 'u8'],
    0x43: ['ref', 'name', 'params'], 0x44: ['name'], 0x45: ['expr', 'expr'],
}


class Func:
    """Parsed UFunction export."""

    def __init__(self, pkg, e):
        self.pkg = pkg
        self.e = e
        d = pkg.data
        r = Reader(d, e['offset'])
        assert not e['flags'] & 0x02000000
        r.ci()  # None tag
        r.ci(); r.ci(); r.ci(); r.ci(); r.ci(); r.ci()  # super next scripttext children friendly cpp
        r.i32(); r.i32()
        self.ssize_pos = r.p
        self.script_size = r.i32()
        self.head = d[e['offset']:self.ssize_pos]
        self.r = r
        self.mem = 0
        self.stmts = []
        while self.mem < self.script_size:
            self.stmts.append(self.parse())
        assert self.mem == self.script_size, (self.mem, self.script_size)
        self.tail = d[r.p:e['offset'] + e['size']]

    # ---- parsing
    def parse(self):
        r = self.r
        old = self.mem
        t = r.u8(); self.mem += 1
        if 0x60 <= t <= 0x6f:
            lo = r.u8(); self.mem += 1
            n = Node(t, [('u8', lo), ('params', self.parse_params())], old)
            return n
        if t >= 0x70:
            return Node(t, [('params', self.parse_params())], old)
        if t not in G:
            raise ValueError(f'unknown token {t:#x} @mem {old}')
        f = []
        for k in G[t]:
            if k == 'ref' or k == 'name':
                f.append((k, r.ci())); self.mem += 4
            elif k == 'expr':
                m0 = self.mem
                f.append((k, self.parse()))
                for kk, vv in f:
                    if kk == 'skip' and vv[1] is None:
                        vv[1] = self.mem - m0
            elif k == 'params':
                f.append((k, self.parse_params()))
            elif k in ('u8', 'vsize'):
                f.append((k, r.u8())); self.mem += 1
            elif k == 'u16':
                f.append((k, r.u16())); self.mem += 2
            elif k == 'jump':
                f.append((k, r.u16())); self.mem += 2
            elif k == 'skip':
                f.append((k, [r.u16(), None])); self.mem += 2
            elif k == 'i32':
                f.append((k, r.i32())); self.mem += 4
            elif k == 'u32':
                f.append((k, r.u32())); self.mem += 4
            elif k == 'f32':
                f.append((k, r.d[r.p:r.p + 4])); r.p += 4; self.mem += 4
            elif k == 'zstr':
                e = r.d.index(b'\0', r.p); s = r.d[r.p:e + 1]; r.p = e + 1
                f.append((k, s)); self.mem += len(s)
            elif k == 'wstr':
                e = r.p
                while r.d[e:e + 2] != b'\0\0':
                    e += 2
                s = r.d[r.p:e + 2]; r.p = e + 2
                f.append((k, s)); self.mem += len(s)
            elif k == 'case':
                nxt = r.u16(); self.mem += 2
                f.append(('jump', nxt))
                f.append(('expr', self.parse() if nxt != 0xFFFF else None))
            elif k == 'labels':
                labs = []
                while True:
                    nm = r.ci(); off = r.u32(); self.mem += 8
                    labs.append((nm, off))
                    if self.pkg.names[nm] == 'None':
                        break
                f.append((k, labs))
        return Node(t, f, old)

    def parse_params(self):
        ps = []
        while True:
            if self.r.d[self.r.p] == 0x16:
                self.r.p += 1; self.mem += 1
                return ps
            ps.append(self.parse())

    # ---- emitting
    def emit(self, stmts=None):
        stmts = self.stmts if stmts is None else stmts
        # pass 1: compute new mem offsets for every node (fields have fixed widths)
        self.remap = {}
        self.newpos = {}
        self._pos = 0
        for s in stmts:
            self._layout(s)
        # pass 2: write
        self._out = bytearray()
        self._pos = 0
        for s in stmts:
            self._write(s)
        size = self._pos
        body = bytes(self.head) + struct.pack('<i', size) + bytes(self._out) + bytes(self.tail)
        return body, size

    def _layout(self, n):
        start = self._pos
        self.newpos[id(n)] = start
        if n.old is not None and n.old not in self.remap:
            self.remap[n.old] = start
        self._pos += 1
        if 0x60 <= n.t <= 0x6f:
            self._pos += 1
            for p in n.f[1][1]: self._layout(p)
            self._pos += 1
            n._size = self._pos - start
            return
        for k, v in n.f:
            if k in ('ref', 'name', 'i32', 'u32', 'f32'):
                self._pos += 4
            elif k in ('u8', 'vsize'):
                self._pos += 1
            elif k in ('u16', 'jump', 'skip'):
                self._pos += 2
            elif k in ('zstr', 'wstr'):
                self._pos += len(v)
            elif k == 'expr':
                if v is not None: self._layout(v)
            elif k == 'params':
                for p in v: self._layout(p)
                self._pos += 1
            elif k == 'labels':
                self._pos += 8 * len(v)
        n._size = self._pos - start

    def _target(self, old):
        if isinstance(old, Node):          # jump to a (possibly new) node
            return self.newpos[id(old)]
        if old == 0xFFFF:
            return 0xFFFF
        if old in self.remap:
            return self.remap[old]
        if old == self.script_size:   # jump to end
            return self._total
        raise KeyError(f'jump target {old:#x} not a node start')

    def _write(self, n):
        o = self._out
        o.append(n.t); self._pos += 1
        if 0x60 <= n.t <= 0x6f:
            o.append(n.f[0][1]); self._pos += 1
            for p in n.f[1][1]: self._write(p)
            o.append(0x16); self._pos += 1
            return
        fl = n.f
        for i, (k, v) in enumerate(fl):
            if k in ('ref', 'name'):
                o += enc_ci(v); self._pos += 4
            elif k == 'u8':
                o.append(v); self._pos += 1
            elif k == 'vsize':
                o.append(v); self._pos += 1
            elif k == 'u16':
                o += struct.pack('<H', v); self._pos += 2
            elif k == 'jump':
                o += struct.pack('<H', self._target(v)); self._pos += 2
            elif k == 'skip':
                # size of the next expr field in memory
                nxt = next(x for kk, x in fl[i + 1:] if kk == 'expr')
                val = nxt._size + (1 if n.t == 0x18 else 0)
                if isinstance(v, list) and v[1] == nxt._size:
                    val = v[0]   # keep compiler's original value for untouched code
                o += struct.pack('<H', val); self._pos += 2
            elif k == 'i32':
                o += struct.pack('<i', v); self._pos += 4
            elif k == 'u32':
                o += struct.pack('<I', v); self._pos += 4
            elif k == 'f32':
                o += v; self._pos += 4
            elif k in ('zstr', 'wstr'):
                o += v; self._pos += len(v)
            elif k == 'expr':
                if v is not None: self._write(v)
            elif k == 'params':
                for p in v: self._write(p)
                o.append(0x16); self._pos += 1
            elif k == 'labels':
                for nm, off in v:
                    o += enc_ci(nm)
                    tgt = off if self.pkg.names[nm] == 'None' else self._target(off)
                    o += struct.pack('<I', tgt); self._pos += 8

    def emit_checked(self, stmts=None):
        stmts = self.stmts if stmts is None else stmts
        # total size needed for jump-to-end targets
        self.remap = {}; self.newpos = {}; self._pos = 0
        for s in stmts: self._layout(s)
        self._total = self._pos
        return self.emit(stmts)


def walk(n, fn, parent=None):
    """post-order replace: fn(node) -> node"""
    for i, (k, v) in enumerate(n.f):
        if k == 'expr' and v is not None:
            n.f[i] = (k, walk(v, fn, n))
        elif k == 'params':
            n.f[i] = (k, [walk(p, fn, n) for p in v])
    return fn(n)


if __name__ == '__main__':
    # round-trip test over all functions of a package
    pkg = Package(sys.argv[1])
    ok = bad = 0
    for e in pkg.exports:
        if pkg.classname(e) != 'Function' or e['size'] == 0:
            continue
        try:
            f = Func(pkg, e)
            body, _ = f.emit_checked()
            orig = pkg.data[e['offset']:e['offset'] + e['size']]
            if body == orig:
                ok += 1
            else:
                bad += 1
                if bad < 5: print('MISMATCH', pkg.fullname(e['idx']))
        except Exception as ex:
            bad += 1
            if bad < 8: print('ERR', pkg.fullname(e['idx']), ex)
    print(f'{os.path.basename(sys.argv[1])}: roundtrip ok={ok} bad={bad}')
