"""Generate a UE2 (package v129) texture package with HD bitmap fonts (Latin + Cyrillic).

Output: SHHDFonts.utx with fonts named <Style><Height>, e.g. Big2160, Med1080 ...
Each font: UFont { Characters[], Textures[], Kerning, CharRemap, IsRemapped=1 },
pages are DXT5 textures (bMasked, white glyphs, alpha = coverage) with full mip chains.
"""
import struct, sys, os
import numpy as np
from PIL import Image, ImageDraw, ImageFont
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ubc import enc_ci

FONT_FILE = os.environ.get('SHR_FONT', 'C:/Windows/Fonts/palab.ttf')   # 0.9.73: Palatino Linotype Bold (the English game's serif; has Cyrillic). Was DejaVu Sans Condensed Bold
SIZES = [8, 10, 12, 14, 16, 18, 20, 24, 26, 30]   # SAPFont.rus pixel sizes at 768 lines
HEIGHTS = [720, 1080, 1440, 1800, 2160]
PAGE = 1024

CHARS = [chr(c) for c in range(32, 127)] + [chr(c) for c in range(0xA0, 0x100)] \
    + [chr(c) for c in range(0x400, 0x460)] \
    + list('–—‘’‚“”„…•№™€«»')
CHARS = list(dict.fromkeys(CHARS))


# ---------------------------------------------------------------- glyph atlas
def build_font(px):
    f = ImageFont.truetype(FONT_FILE, px)
    asc, desc = f.getmetrics()
    line_h = asc + desc
    cells = []
    for ch in CHARS:
        adv = int(round(f.getlength(ch))) or max(1, px // 4)
        cells.append((ch, adv))
    pages, chars = [], []
    x = y = 1
    img = Image.new('L', (PAGE, PAGE), 0); dr = ImageDraw.Draw(img)
    for ch, adv in cells:
        if x + adv + 1 > PAGE:
            x = 1; y += line_h + 2
        if y + line_h + 1 > PAGE:
            pages.append(img); img = Image.new('L', (PAGE, PAGE), 0); dr = ImageDraw.Draw(img); x = y = 1
        dr.text((x, y), ch, font=f, fill=255)
        chars.append((x, y, adv, line_h, len(pages)))
        x += adv + 2
    pages.append(img)
    # shrink last page to the smallest power of two that holds the used area
    out = []
    for i, p in enumerate(pages):
        used_h = max((c[1] + c[3] for c in chars if c[4] == i), default=1) + 1
        h = PAGE
        while h // 2 >= used_h:
            h //= 2
        out.append(p.crop((0, 0, PAGE, h)))
    return chars, out


# ---------------------------------------------------------------- DXT5 encoder (white rgb, alpha only matters)
def dxt5(alpha):
    h, w = alpha.shape
    bw, bh = (w + 3) // 4, (h + 3) // 4
    a = np.zeros((bh * 4, bw * 4), np.uint8)
    a[:h, :w] = alpha
    blk = a.reshape(bh, 4, bw, 4).transpose(0, 2, 1, 3).reshape(bh * bw, 16).astype(np.int32)
    a0 = blk.max(1); a1 = blk.min(1)
    # 8-alpha mode (a0 > a1): palette a0,a1, 6 interpolated
    pal = np.stack([a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)], 1)
    flat = a0 == a1
    idx = np.abs(blk[:, :, None] - pal[:, None, :]).argmin(2).astype(np.uint64)
    idx[flat] = 0
    bits = np.zeros(len(blk), np.uint64)
    for i in range(16):
        bits |= idx[:, i] << np.uint64(3 * i)
    out = np.zeros((len(blk), 16), np.uint8)
    out[:, 0] = a0; out[:, 1] = a1
    for i in range(6):
        out[:, 2 + i] = ((bits >> np.uint64(8 * i)) & np.uint64(0xFF)).astype(np.uint8)
    # colour block: c0 = c1 = white (0xFFFF), indices 0
    out[:, 8] = 0xFF; out[:, 9] = 0xFF; out[:, 10] = 0xFF; out[:, 11] = 0xFF
    return out.tobytes()


def mip_chain(img):
    a = np.asarray(img, np.float32)
    mips = []
    while True:
        h, w = a.shape
        mips.append((w, h, dxt5(np.clip(a + 0.5, 0, 255).astype(np.uint8))))
        if w == 1 and h == 1:
            break
        nw, nh = max(1, w // 2), max(1, h // 2)
        a = a.reshape(nh, h // nh, nw, w // nw).mean((1, 3))
    return mips


# ---------------------------------------------------------------- package writer
class Pkg:
    def __init__(self):
        self.names = []
        self.nidx = {}
        self.imports = []
        self.exports = []

    def name(self, s):
        if s not in self.nidx:
            self.nidx[s] = len(self.names); self.names.append(s)
        return self.nidx[s]

    def imp(self, class_pkg, class_name, package, name):
        self.imports.append((self.name(class_pkg), self.name(class_name), package, self.name(name)))
        return -len(self.imports)

    def exp(self, cls, package, name, flags, body_fn):
        self.exports.append(dict(cls=cls, package=package, name=self.name(name), flags=flags, body=body_fn))
        return len(self.exports)

    def write(self, path):
        for s in ('None',):
            self.name(s)
        hdr_size = 64
        out = bytearray(hdr_size)
        name_off = len(out)
        for s in self.names:
            b = s.encode('latin-1') + b'\0'
            out += enc_ci(len(b)) + b + struct.pack('<I', 0x00070010)
        for e in self.exports:
            e['offset'] = len(out)
            body = e['body'](len(out))
            e['size'] = len(body)
            out += body
        imp_off = len(out)
        for cp, cn, pkg, nm in self.imports:
            out += enc_ci(cp) + enc_ci(cn) + struct.pack('<i', pkg) + enc_ci(nm)
        exp_off = len(out)
        for e in self.exports:
            out += enc_ci(e['cls']) + enc_ci(0) + struct.pack('<i', e['package']) + enc_ci(e['name'])
            out += struct.pack('<I', e['flags']) + enc_ci(e['size'])
            if e['size']:
                out += enc_ci(e['offset'])
        guid = bytes.fromhex('5348484446304e5453323030355f5631')
        struct.pack_into('<IHHI6I16sIII', out, 0, 0x9E2A83C1, 129, 0, 0x1, len(self.names), name_off,
                         len(self.exports), exp_off, len(self.imports), imp_off, guid, 1,
                         len(self.exports), len(self.names))
        open(path, 'wb').write(out)
        return len(out)


def tag_byte(p, name, v):
    return enc_ci(p.name(name)) + bytes([0x01, v])

def tag_int(p, name, v):
    return enc_ci(p.name(name)) + bytes([0x22]) + struct.pack('<i', v)

def tag_bool(p, name, v):
    return enc_ci(p.name(name)) + bytes([0x83 if v else 0x03])


def texture_body(p, img):
    w, h = img.size
    ub, vb = w.bit_length() - 1, h.bit_length() - 1
    mips = mip_chain(img)
    def body(file_off):
        b = bytearray()
        b += tag_bool(p, 'bMasked', True)
        b += tag_byte(p, 'Format', 8)          # TEXF_DXT5
        b += tag_byte(p, 'UBits', ub) + tag_byte(p, 'VBits', vb)
        b += tag_int(p, 'USize', w) + tag_int(p, 'VSize', h)
        b += tag_int(p, 'UClamp', w) + tag_int(p, 'VClamp', h)
        b += enc_ci(p.name('None'))
        b += enc_ci(len(mips))
        for mw, mh, data in mips:
            cnt = enc_ci(len(data))
            skip = file_off + len(b) + 4 + len(cnt) + len(data)   # absolute end of lazy data
            b += struct.pack('<i', skip) + cnt + data
            b += struct.pack('<ii', mw, mh) + bytes([max(0, mw.bit_length() - 1), max(0, mh.bit_length() - 1)])
        return bytes(b)
    return body


def font_body(p, chars, tex_refs):
    def body(file_off):
        b = bytearray(enc_ci(p.name('None')))
        b += enc_ci(len(chars))
        for x, y, w, h, page in chars:
            b += struct.pack('<iiiiB', x, y, w, h, page)
        b += enc_ci(len(tex_refs))
        for r in tex_refs:
            b += enc_ci(r)
        b += struct.pack('<i', 0)                         # Kerning
        b += enc_ci(len(CHARS))                           # CharRemap: unicode -> index
        for i, ch in enumerate(CHARS):
            b += struct.pack('<HH', ord(ch), i)
        b += struct.pack('<i', 1)                         # IsRemapped
        return bytes(b)
    return body


def main(out_path, preview_dir=None):
    p = Pkg()
    for n in ('None', 'bMasked', 'Format', 'UBits', 'VBits', 'USize', 'VSize', 'UClamp', 'VClamp'):
        p.name(n)
    core = p.imp('Core', 'Package', 0, 'Core')
    eng = p.imp('Core', 'Package', 0, 'Engine')
    cls_font = p.imp('Core', 'Class', eng, 'Font')
    cls_tex = p.imp('Core', 'Class', eng, 'Texture')
    FLAGS_FONT = 0x000F0004
    FLAGS_TEX = 0x00070000
    report = []
    for h in HEIGHTS:
        for base in SIZES:
            px = max(8, round(base * h / 768))
            chars, pages = build_font(px)
            fname = f'S{base}_{h}'
            # font export first (outer of its pages), textures reference it as package
            fidx = len(p.exports) + 1
            tex_refs = [fidx + 1 + i for i in range(len(pages))]
            p.exp(cls_font, 0, fname, FLAGS_FONT, font_body(p, chars, tex_refs))
            for i, img in enumerate(pages):
                p.exp(cls_tex, fidx, f'{fname}_Page{chr(65 + i)}', FLAGS_TEX, texture_body(p, img))
                if preview_dir and h == 2160:
                    img.save(os.path.join(preview_dir, f'{fname}_{i}.png'))
            report.append(f'{fname}: {px}px, line {chars[0][3]}, pages {[im.size for im in pages]}')
    size = p.write(out_path)
    print('\n'.join(report))
    print('written', out_path, size, 'bytes')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
