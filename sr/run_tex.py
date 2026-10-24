import sys, os, glob, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from PIL import Image
from srnet import RRDB
lo, hi = sys.argv[1], sys.argv[2]
m = RRDB(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'x4plus.pth'))
files = sorted(glob.glob(os.path.join(lo, '*.png')), key=os.path.getsize)
t0 = time.time()
for i, f in enumerate(files):
    out = os.path.join(hi, os.path.basename(f))
    if os.path.exists(out + '.done'): continue
    im = Image.open(f); grey = im.mode == 'L'
    a = np.asarray(im.convert('RGB'))
    h, w = a.shape[:2]
    # tile to bound memory
    T = 192; P = 8; y = np.zeros((h * 4, w * 4, 3), np.float32)
    for ty in range(0, h, T):
        for tx in range(0, w, T):
            y0, x0 = max(0, ty - P), max(0, tx - P); y1, x1 = min(h, ty + T + P), min(w, tx + T + P)
            r = m(np.ascontiguousarray(a[y0:y1, x0:x1]))
            y[ty * 4:min(h, ty + T) * 4, tx * 4:min(w, tx + T) * 4] = r[(ty - y0) * 4:(ty - y0) * 4 + (min(h, ty + T) - ty) * 4, (tx - x0) * 4:(tx - x0) * 4 + (min(w, tx + T) - tx) * 4]
    img = Image.fromarray((y * 255 + .5).clip(0, 255).astype(np.uint8))
    if grey: img = img.convert('L')
    img.save(out); open(out + '.done', 'w').close()
    print(f'{i + 1}/{len(files)} {os.path.basename(f)} {time.time() - t0:.0f}s', flush=True)
