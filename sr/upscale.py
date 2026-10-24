"""Decode .bik -> Real-ESRGAN x4 -> Lanczos to WxH -> raw yuv420p (BT.601 limited)."""
import subprocess, sys, time, os
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from srnet import SR

src, dst, W, H = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
skip = int(sys.argv[5]) if len(sys.argv) > 5 else 0
pr = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_entries', 'stream=width,height', '-of', 'csv=p=0', src], capture_output=True, text=True).stdout.strip().split(',')
w, h = int(pr[0]), int(pr[1])
sr = SR(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'animevideov3.pth'), 4)
dec = subprocess.Popen(['ffmpeg', '-v', 'error', '-i', src, '-map', '0:v:0', '-fps_mode', 'passthrough', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'], stdout=subprocess.PIPE)
enc = subprocess.Popen(['ffmpeg', '-v', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb48le', '-s', f'{w*4}x{h*4}', '-i', '-',
                        '-vf', f'scale={W}:{H}:flags=lanczos:out_color_matrix=bt601:out_range=tv', '-pix_fmt', 'yuv420p', '-f', 'rawvideo', dst + '.part'], stdin=subprocess.PIPE)
n = 0; t0 = time.time()
for _ in range(skip): dec.stdout.read(w * h * 3)
while True:
    b = dec.stdout.read(w * h * 3)
    if len(b) < w * h * 3: break
    y = sr(np.frombuffer(b, np.uint8).reshape(h, w, 3))
    enc.stdin.write((y * 65535 + 0.5).astype('<u2').tobytes())
    n += 1
    if n % 20 == 0: print(f'{os.path.basename(src)} {n} frames {(time.time()-t0)/n:.2f}s/f', flush=True)
enc.stdin.close(); enc.wait(); dec.wait()
os.rename(dst + '.part', dst)
print('done', src, n, 'frames', flush=True)
