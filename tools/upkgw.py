"""Rebuild a UE2 package with replaced export bodies / appended imports."""
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upkg import Package
from ubc import enc_ci


def _ci_at(b, p):
    b0 = b[p]; v = b0 & 0x3F; n = 1
    if b0 & 0x40:
        sh = 6
        for _ in range(4):
            x = b[p + n]; n += 1; v |= (x & 0x7F) << sh; sh += 7
            if not x & 0x80: break
    return (-v if b0 & 0x80 else v), n


def fix_lazy(body, old_off, delta):
    """TLazyArray<BYTE> stores an absolute file offset (SkipPos) right before its
    data: [i32 SkipPos][ci count][count bytes], SkipPos == offset after the data."""
    b = bytearray(body)
    n = 0
    p = 0
    end = old_off + len(b)
    while p + 5 <= len(b):
        v = struct.unpack_from('<i', b, p)[0]
        if old_off < v <= end:
            cnt, ln = _ci_at(b, p + 4)
            if cnt > 0 and old_off + p + 4 + ln + cnt == v:
                struct.pack_into('<i', b, p, v + delta)
                n += 1
                p = v - old_off
                continue
        p += 1
    return bytes(b), n


def rebuild(pkg, new_bodies, new_imports=()):
    """new_bodies: {export_idx(1-based): bytes}. new_imports: list of dicts with
    class_pkg/class_name/package/name (names must exist)."""
    d = pkg.data
    h = struct.unpack_from('<IHHI6I', d, 0)
    nc, no, ec, eo, ic, io = h[4:]
    exps = [dict(e) for e in pkg.exports]
    data_start = min(e['offset'] for e in exps if e['size'] > 0)
    order = sorted((e for e in exps if e['size'] > 0), key=lambda e: e['offset'])
    out = bytearray(d[:data_start])
    fixed_lazy = []
    pos = data_start
    for e in order:
        assert e['offset'] == pos, ('gap', e['name'], e['offset'], pos)
        body = new_bodies.get(e['idx'], d[e['offset']:e['offset'] + e['size']])
        if callable(body):
            body = body(len(out))
        pos += e['size']
        delta = len(out) - e['offset']
        if delta and e['idx'] not in new_bodies and pkg.classname(e) == 'Texture':
            body, nfix = fix_lazy(body, e['offset'], delta)
            fixed_lazy.append((e['name'], nfix))
        e['offset'] = len(out)
        e['size'] = len(body)
        out += body
    assert pos == io, ('data does not end at import table', pos, io)
    name_idx = {n: i for i, n in enumerate(pkg.names)}
    imps = list(pkg.imports) + list(new_imports)
    new_io = len(out)
    for im in imps:
        out += enc_ci(name_idx[im['class_pkg']]) + enc_ci(name_idx[im['class_name']])
        out += struct.pack('<i', im['package']) + enc_ci(name_idx[im['name']])
    new_eo = len(out)
    for e in exps:
        out += enc_ci(e['cls']) + enc_ci(e['super']) + struct.pack('<i', e['package'])
        out += enc_ci(name_idx[e['name']]) + struct.pack('<I', e['flags']) + enc_ci(e['size'])
        if e['size'] > 0:
            out += enc_ci(e['offset'])
    tail_start = eo + sum(1 for _ in ())  # placeholder
    rebuild.fixed_lazy = fixed_lazy
    struct.pack_into('<IHHI6I', out, 0, h[0], h[1], h[2], h[3], nc, no, ec, new_eo, len(imps), new_io)
    return bytes(out)


if __name__ == '__main__':
    p = Package(sys.argv[1])
    b = rebuild(p, {})
    print(os.path.basename(sys.argv[1]), 'identical' if b == p.data else f'DIFF len {len(b)} vs {len(p.data)}')
