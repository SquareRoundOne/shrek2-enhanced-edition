"""Bink 1 container reader/writer."""
import struct

class Bik:
    def __init__(self, path):
        d = open(path, 'rb').read()
        self.d = d
        (self.sig, fsize, self.nframes, self.maxframe, self.unk, self.w, self.h,
         self.fpsn, self.fpsd, self.flags, self.ntracks) = struct.unpack_from('<4s10I', d, 0)
        p = 44
        self.tr_maxsize = list(struct.unpack_from(f'<{self.ntracks}I', d, p)); p += 4 * self.ntracks
        self.tr_info = [struct.unpack_from('<HH', d, p + 4 * i) for i in range(self.ntracks)]; p += 4 * self.ntracks
        self.tr_id = list(struct.unpack_from(f'<{self.ntracks}I', d, p)); p += 4 * self.ntracks
        idx = list(struct.unpack_from(f'<{self.nframes + 1}I', d, p)); p += 4 * (self.nframes + 1)
        self.hdr_end = p
        self.key = [x & 1 for x in idx[:-1]]
        offs = [x & ~1 for x in idx]
        self.frames = []
        for i in range(self.nframes):
            fr = d[offs[i]:offs[i + 1]]
            q = 0; aud = []
            for t in range(self.ntracks):
                n = struct.unpack_from('<I', fr, q)[0]; aud.append(fr[q + 4:q + 4 + n]); q += 4 + n
            self.frames.append((aud, fr[q:]))

def write_bik(path, sig, w, h, fpsn, fpsd, flags, tracks, frames, keys=None, unk=None):
    """tracks: list of (maxsize, rate, aflags, id). frames: list of (audio_list, video_bytes)."""
    nt = len(tracks); nf = len(frames)
    hdr = 44 + 12 * nt + 4 * (nf + 1)
    body = bytearray(); offs = []
    for i, (aud, vid) in enumerate(frames):
        offs.append(hdr + len(body))
        for a in aud:
            body += struct.pack('<I', len(a)) + a
        body += vid
        while len(body) % 4: body += b'\0'   # frames are dword aligned (offset LSB is the key flag)
    offs.append(hdr + len(body))
    sizes = [offs[i + 1] - offs[i] for i in range(nf)]
    out = bytearray(struct.pack('<4s10I', sig, hdr + len(body) - 8, nf, max(sizes),
                                nf if unk is None else unk, w, h, fpsn, fpsd, flags, nt))
    for t in tracks: out += struct.pack('<I', t[0])
    for t in tracks: out += struct.pack('<HH', t[1], t[2])
    for t in tracks: out += struct.pack('<I', t[3])
    for i in range(nf + 1):
        k = (keys[i] if keys else (i == 0)) if i < nf else 0
        out += struct.pack('<I', offs[i] | k)
    out += body
    open(path, 'wb').write(out)

if __name__ == '__main__':
    import sys
    b = Bik(sys.argv[1])
    print(b.sig, b.nframes, b.maxframe, b.unk, b.w, b.h, b.fpsn, b.fpsd, hex(b.flags), b.ntracks, b.tr_maxsize, b.tr_info, b.tr_id, 'keys', sum(b.key))
    for i in range(min(6, b.nframes)):
        aud, vid = b.frames[i]
        f32 = struct.unpack_from('<I', vid, 0)[0]
        print(i, [len(a) for a in aud], len(vid), 'first32', f32, hex(f32))
