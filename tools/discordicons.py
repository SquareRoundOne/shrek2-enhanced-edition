r"""Level pictures of the launcher (storybookanimTX.utx, 512x512) as PNG files for Discord's Art Assets.
usage: discordicons.py <Textures\storybookanimTX.utx> <out dir>
The file names are the asset keys the game sends (pad/dinput8.cpp discordTick): upload every PNG under Rich Presence > Art Assets."""
import os, struct, sys, zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upkg import Package
from utex import read_texture

ICONS = {   # texture -> asset key
    '1_Shreks_Swamp': 'lv_swamp', '2_Carraige_Hijack': 'lv_carriage', '3_The_Hunt': 'lv_hunt', '4_FGM_PIB': 'lv_fgm',
    '5_FGM_Donkey': 'lv_fgm_donkey', '6_Hamlet': 'lv_hamlet', '7_Prison_Donkey': 'lv_prison_donkey', '8_Prison_PIB': 'lv_prison_pib',
    '9_Prison_Shrek': 'lv_prison_shrek', '10_Castle_Seige': 'lv_castle', '11_FGM_Battle': 'lv_battle', 'giant': 'lv_beanstalk',
}


def rgb565(c):
    return [((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31]


def dxt5(data, w, h):
    out = bytearray(w * h * 4); pos = 0
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            a0, a1 = data[pos], data[pos + 1]; ab = int.from_bytes(data[pos + 2:pos + 8], 'little'); pos += 8
            al = [a0, a1] + ([((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)] if a0 > a1 else [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255])
            c0, c1, cb = struct.unpack_from('<HHI', data, pos); pos += 8
            C0, C1 = rgb565(c0), rgb565(c1)
            cols = [C0, C1, [(2 * C0[i] + C1[i]) // 3 for i in range(3)], [(C0[i] + 2 * C1[i]) // 3 for i in range(3)]]
            for i in range(16):
                x, y = bx + i % 4, by + i // 4; o = (y * w + x) * 4
                out[o:o + 3] = bytes(cols[(cb >> (2 * i)) & 3]); out[o + 3] = al[(ab >> (3 * i)) & 7]
    return out


def png(path, w, h, rgba):
    raw = b''.join(b'\0' + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    def ch(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d))
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw)) + ch(b'IEND', b''))


if __name__ == '__main__':
    p = Package(sys.argv[1]); os.makedirs(sys.argv[2], exist_ok=True)
    for e in p.exports:
        n = p.fullname(e['idx']).split('.')[-1]
        if n in ICONS and p.classname(e) == 'Texture':
            props, mips, _ = read_texture(p, e); m = mips[0]
            png(os.path.join(sys.argv[2], ICONS[n] + '.png'), m['w'], m['h'], dxt5(p.data[m['off']:m['off'] + m['size']], m['w'], m['h']))
            print(n, '->', ICONS[n] + '.png', m['w'], m['h'])
