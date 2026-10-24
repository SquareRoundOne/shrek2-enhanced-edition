"""HD texture packages for Shrek 2: Real-ESRGAN upscale of world textures, new mip chains, DXT re-encode.

usage: texsr.py <in.utx> <out.utx> [--preview dir] [--limit N]

Per texture (DXT1/DXT3/DXT5/RGBA8/P8, >= 16 px):
  * top mip decoded to RGBA, wrap-padded (most world textures tile) and upscaled x4 with
    realesr-general-x4v3; alpha is upscaled by the same network as a grey image;
  * final size: x4, or x2 when the original is already >= 512 (cap 2048);
  * full mip chain (box filter in linear light; alpha-tested textures keep their coverage);
  * DXT1 when fully opaque and not flagged as alpha/masked, otherwise DXT5; RGBA8 stays RGBA8;
  * only USize/VSize/UBits/VBits/UClamp/VClamp/Format change in the property block.
Texture names, materials and every reference stay the same, so the game needs no other change.
"""
import ctypes, os, struct, sys, time
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upkg import Package, Reader
from upkgw import rebuild
from ubc import enc_ci
from utex import read_texture, FMT

HERE = os.path.dirname(os.path.abspath(__file__))
SCR = os.path.dirname(HERE)
dxt = ctypes.CDLL(os.path.join(SCR, 'dxt', 'libdxt.so'))
sys.path.insert(0, os.path.join(SCR, 'sr'))
from srnet import SR

_sr = None
def sr4(rgb):
    global _sr
    if _sr is None:
        _sr = SR(os.path.join(SCR, 'sr', 'general-x4v3.pth'), 4)
    return _sr(rgb)


# ------------------------------------------------------------------ decode
def decode(pkg, props, mip):
    f = FMT.get(props.get('Format', 0))
    w, h = mip['w'], mip['h']
    raw = pkg.data[mip['off']:mip['off'] + mip['size']]
    out = np.zeros((h, w, 4), np.uint8)
    if f in ('DXT1', 'DXT3', 'DXT5'):
        code = {'DXT1': 1, 'DXT3': 3, 'DXT5': 5}[f]
        buf = ctypes.create_string_buffer(w * h * 4)
        dxt.dxt_decode(raw, w, h, code, buf)
        out = np.frombuffer(buf.raw, np.uint8).reshape(h, w, 4).copy()
    elif f == 'RGBA8':
        a = np.frombuffer(raw, np.uint8).reshape(h, w, 4)
        out = a[:, :, [2, 1, 0, 3]].copy()            # stored BGRA
    elif f == 'P8':
        pal = palette(pkg, props['Palette'])
        idx = np.frombuffer(raw, np.uint8).reshape(h, w)
        out = pal[idx]
        if props.get('bMasked'):
            out[idx == 0, 3] = 0
    else:
        return None
    return out

def palette(pkg, ref):
    e = pkg.exports[ref - 1]
    r = Reader(pkg.data, e['offset'])
    assert pkg.names[r.ci()] == 'None'
    n = r.ci()
    a = np.frombuffer(pkg.data[r.p:r.p + 4 * n], np.uint8).reshape(n, 4)   # FColor B,G,R,A on disk
    pal = np.zeros((256, 4), np.uint8); pal[:n] = a[:, [2, 1, 0, 3]]
    return pal


# ------------------------------------------------------------------ upscale
def upscale(rgba, factor, has_alpha):
    h, w = rgba.shape[:2]
    pad = max(4, min(16, w // 4, h // 4))
    def run(img3):
        p = np.pad(img3, ((pad, pad), (pad, pad), (0, 0)), mode='wrap')
        y = sr4(p)[pad * 4:pad * 4 + h * 4, pad * 4:pad * 4 + w * 4]
        if factor == 2:
            y = y.reshape(h * 2, 2, w * 2, 2, 3).mean((1, 3))
        return y
    rgb = run(rgba[:, :, :3])
    if has_alpha:
        a = rgba[:, :, 3:4]
        al = run(np.repeat(a, 3, 2)).mean(2, keepdims=True)
    else:
        al = np.ones(rgb.shape[:2] + (1,), np.float32)
    return np.concatenate([rgb, al], 2)             # float 0..1


# ------------------------------------------------------------------ mips + encode
def to_lin(x): return np.power(np.clip(x, 0, 1), 2.2)
def to_srgb(x): return np.power(np.clip(x, 0, 1), 1 / 2.2)

def coverage(a, thr=0.5):
    return float((a > thr).mean())

def mip_chain(img, masked):
    """img float HxWx4 (sRGB colour, linear alpha) -> list of uint8 RGBA mips down to 1x1"""
    mips = []
    cur_lin = np.concatenate([to_lin(img[:, :, :3]), img[:, :, 3:4]], 2)
    cov0 = coverage(img[:, :, 3]) if masked else None
    while True:
        h, w = cur_lin.shape[:2]
        rgb = to_srgb(cur_lin[:, :, :3]); a = cur_lin[:, :, 3:4]
        if masked and cov0 and mips:
            # scale alpha so that the alpha-test coverage matches the top mip
            lo, hi = 0.0, 4.0
            for _ in range(16):
                m = (lo + hi) / 2
                if coverage(np.clip(a * m, 0, 1)) < cov0: lo = m
                else: hi = m
            a = np.clip(a * hi, 0, 1)
        mips.append((np.concatenate([rgb, a], 2) * 255 + 0.5).clip(0, 255).astype(np.uint8))
        if w == 1 and h == 1:
            break
        nw, nh = max(1, w // 2), max(1, h // 2)
        cur_lin = cur_lin.reshape(nh, h // nh, nw, w // nw, 4).mean((1, 3))
    return mips

def encode(m, fmt):
    h, w = m.shape[:2]
    if fmt == 'RGBA8':
        return m[:, :, [2, 1, 0, 3]].tobytes()
    code = 1 if fmt == 'DXT1' else 5
    bw, bh = (w + 3) // 4, (h + 3) // 4
    out = ctypes.create_string_buffer(bw * bh * (8 if code == 1 else 16))
    dxt.dxt_encode(np.ascontiguousarray(m).tobytes(), w, h, code, 12, out)
    return out.raw


# ------------------------------------------------------------------ property patch
FMT_ID = {'DXT1': 3, 'DXT5': 8, 'RGBA8': 5}

def patch_props(pkg, e, new):
    """rewrite int/byte values of existing tags; append missing ones before None"""
    d = pkg.data
    r = Reader(d, e['offset'])
    out = bytearray(); seen = set()
    while True:
        start = r.p
        ni = r.ci(); nm = pkg.names[ni]
        if nm == 'None':
            for k, v in new.items():
                if k in seen: continue
                if k not in pkg.names: raise KeyError(f'name {k} missing in package')
                kind, val = v
                if kind == 'byte': out += enc_ci(pkg.names.index(k)) + bytes([0x01, val])
                else: out += enc_ci(pkg.names.index(k)) + bytes([0x02]) + struct.pack('<i', val)
            out += d[start:r.p]
            return bytes(out), r.p
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
        hdr = d[start:r.p]; val = d[r.p:r.p + size]; r.p += size
        if nm in new:
            seen.add(nm)
            kind, v = new[nm]
            val = bytes([v]) if typ == 1 else struct.pack('<i', v)
        out += hdr + val


def texture_body(pkg, e, mips_bytes, sizes, props_new):
    props_bytes, _ = patch_props(pkg, e, props_new)
    def body(file_off):
        b = bytearray(props_bytes)
        b += enc_ci(len(mips_bytes))
        for data, (w, h) in zip(mips_bytes, sizes):
            cnt = enc_ci(len(data))
            skip = file_off + len(b) + 4 + len(cnt) + len(data)
            b += struct.pack('<i', skip) + cnt + data
            b += struct.pack('<ii', w, h) + bytes([max(0, w.bit_length() - 1), max(0, h.bit_length() - 1)])
        return bytes(b)
    return body


def process(path, out, preview=None, limit=None):
    pkg = Package(path)
    bodies = {}; n = 0; t0 = time.time(); stats = []
    for e in pkg.exports:
        if pkg.classname(e) != 'Texture' or not e['size']:
            continue
        props, mips, tail = read_texture(pkg, e)
        if not mips or tail:
            continue
        f = FMT.get(props.get('Format', 0))
        w, h = mips[0]['w'], mips[0]['h']
        if f not in ('DXT1', 'DXT3', 'DXT5', 'RGBA8', 'P8') or min(w, h) < 16 or w & (w - 1) or h & (h - 1):
            continue
        if f == 'P8' and 'Palette' not in props:
            continue
        rgba = decode(pkg, props, mips[0])
        if rgba is None:
            continue
        has_alpha = bool((rgba[:, :, 3] < 255).any())
        masked = bool(props.get('bMasked'))
        factor = 4 if max(w, h) < 512 else 2
        if max(w, h) * factor > 2048:
            factor = 2048 // max(w, h)
        if factor < 2:
            continue
        hi = upscale(rgba, factor, has_alpha)
        nf = 'RGBA8' if f == 'RGBA8' else ('DXT5' if (has_alpha or props.get('bAlphaTexture') or masked) else 'DXT1')
        chain = mip_chain(hi, masked and has_alpha)
        data = [encode(m, nf) for m in chain]
        sizes = [(m.shape[1], m.shape[0]) for m in chain]
        W, H = sizes[0]
        new = {'USize': ('int', W), 'VSize': ('int', H), 'UClamp': ('int', W), 'VClamp': ('int', H),
               'UBits': ('byte', W.bit_length() - 1), 'VBits': ('byte', H.bit_length() - 1),
               'Format': ('byte', FMT_ID[nf])}
        bodies[e['idx']] = texture_body(pkg, e, data, sizes, new)
        n += 1
        stats.append((e['name'], f, w, h, nf, W, H))
        if preview and n <= 400:
            Image.fromarray(rgba).save(os.path.join(preview, f"{e['name']}_lo.png"))
            Image.fromarray(chain[0]).save(os.path.join(preview, f"{e['name']}_hi.png"))
        print(f"  {e['name']}: {f} {w}x{h} -> {nf} {W}x{H}  ({time.time() - t0:.0f}s)", flush=True)
        if limit and n >= limit:
            break
    open(out, 'wb').write(rebuild(pkg, bodies))
    print(f'{os.path.basename(path)}: {n} textures upscaled, {os.path.getsize(path) / 1e6:.1f} -> {os.path.getsize(out) / 1e6:.1f} MB, {time.time() - t0:.0f}s')
    return stats


if __name__ == '__main__':
    args = sys.argv[1:]
    preview = limit = None
    if '--preview' in args:
        i = args.index('--preview'); preview = args[i + 1]; os.makedirs(preview, exist_ok=True); del args[i:i + 2]
    if '--limit' in args:
        i = args.index('--limit'); limit = int(args[i + 1]); del args[i:i + 2]
    process(args[0], args[1], preview, limit)
