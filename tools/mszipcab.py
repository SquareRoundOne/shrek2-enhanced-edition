"""Minimal MS-CAB (MSZIP) extractor: python3 mszipcab.py <cab> <outdir>"""
import struct, sys, os, zlib
cab, out = sys.argv[1], sys.argv[2]
f = open(cab, 'rb')
h = f.read(36)
sig, _, cb, _, coff, _, vmin, vmaj, nfold, nfiles, flags, setid, icab = struct.unpack('<4sIIIIIBBHHHHH', h)
assert sig == b'MSCF' and flags == 0
folders = [struct.unpack('<IHH', f.read(8)) for _ in range(nfold)]
f.seek(coff)
files = []
for _ in range(nfiles):
    size, off, ifold, date, time, attr = struct.unpack('<IIHHHH', f.read(16))
    name = b''
    while (c := f.read(1)) != b'\0': name += c
    files.append((name.decode('latin-1'), size, off, ifold))
os.makedirs(out, exist_ok=True)
byfold = {}
for fl in files: byfold.setdefault(fl[3], []).append(fl)
for fi, (start, ndata, comp) in enumerate(folders):
    assert comp & 0xF == 1, comp
    f.seek(start)
    buf = bytearray(); prev = b''
    for _ in range(ndata):
        csum, cbd, cbu = struct.unpack('<IHH', f.read(8))
        blk = f.read(cbd)
        assert blk[:2] == b'CK'
        d = zlib.decompressobj(-15, zdict=prev) if prev else zlib.decompressobj(-15)
        o = d.decompress(blk[2:]) + d.flush()
        assert len(o) == cbu, (len(o), cbu)
        buf += o; prev = bytes(buf[-32768:])
    for name, size, off, _ in byfold.get(fi, []):
        open(os.path.join(out, name), 'wb').write(buf[off:off + size])
print(len(files), 'files')
