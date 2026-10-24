import sys, os, struct, glob, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upkg import Package
from uprops import read_props
def bink_audio(b):
    # Bink 1 header: 'BIK'+ver, size, frames, maxframe, frames2, w, h, fpsnum, fpsden, flags, ntracks; then per track: 4 bytes (max packet), then 4 bytes (rate u16, flags u16), then track id
    if b[:3] != b'BIK': return None
    n = struct.unpack_from('<I', b, 40)[0]
    if n < 1 or n > 8: return None
    p = 44 + 4 * n
    rate, flags = struct.unpack_from('<HH', b, p)
    frames = struct.unpack_from('<I', b, 8)[0]; fn, fd = struct.unpack_from('<II', b, 28)
    return rate, 2 if flags & 0x2000 else 1, frames * fd / max(fn, 1)
stats = collections.Counter(); dur = collections.Counter()
for f in sorted(glob.glob(sys.argv[1] + '/*.uax')) + sorted(glob.glob(sys.argv[1] + '/*.Rus_uax')):
    p = Package(f); n = 0; types = collections.Counter(); rates = collections.Counter(); secs = 0
    for e in p.exports:
        if p.classname(e) != 'Sound' or e['size'] <= 0: continue
        props, end = read_props(p, e)
        body = p.data[end:e['offset'] + e['size']]
        i = body.find(b'BIK'); r = body.find(b'RIFF')
        if i >= 0 and (r < 0 or i < r):
            a = bink_audio(body[i:])
            types['bik'] += 1
            if a: rates[(a[0], a[1])] += 1; secs += a[2]
        elif r >= 0:
            types['wav'] += 1
            rate = struct.unpack_from('<I', body, r + 24)[0]; ch = struct.unpack_from('<H', body, r + 22)[0]; rates[(rate, ch)] += 1
        else: types['?'] += 1
        n += 1
    print(f'{os.path.basename(f):24s} {n:5d} sounds {dict(types)} rates {dict(rates)} ~{secs/60:.1f} min')
