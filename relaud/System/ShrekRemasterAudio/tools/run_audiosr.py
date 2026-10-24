# AudioSR over a list of WAV files, without its command line tool (that one needs ffmpeg to save):
# the model loads once, every result goes to <out>/<same name>.wav (48 kHz float). Stereo files are
# done channel by channel (AudioSR itself works in mono). Files already there are skipped, so an
# interrupted run continues where it stopped.
#   python run_audiosr.py <list.txt> <out dir> <ddim steps> [basic|speech]
import os, sys, time, tempfile
import numpy as np
import soundfile as sf
import torch
from audiosr import build_model, super_resolution

lst, out, steps = sys.argv[1], sys.argv[2], int(sys.argv[3])
model_name = sys.argv[4] if len(sys.argv) > 4 else "basic"
os.makedirs(out, exist_ok=True)
files = [l.strip() for l in open(lst, encoding="utf-8") if l.strip()]
todo = [f for f in files if not os.path.exists(os.path.join(out, os.path.splitext(os.path.basename(f))[0] + ".wav"))]
if not todo:
    sys.exit(0)
dev = "cuda" if torch.cuda.is_available() else "cpu"
print("AudioSR %s on %s (%s), %d files" % (model_name, dev, torch.cuda.get_device_name(0) if dev == "cuda" else "no CUDA", len(todo)), flush=True)
model = build_model(model_name=model_name, device=dev)
tmpdir = tempfile.mkdtemp(prefix="shrek_sr_")

from math import gcd
from scipy.signal import resample_poly

def plain48(seg, rate):
    """plain 48 kHz resample: what a piece becomes when AudioSR cannot take it"""
    g = gcd(48000, rate)
    return resample_poly(seg, 48000 // g, rate // g).astype(np.float32)

fallbacks = []

def sr_file(path):
    w = super_resolution(model, path, seed=42, ddim_steps=steps, guidance_scale=3.5)
    return np.asarray(w, dtype=np.float32).reshape(-1)

CHUNK, OVER = 5.12, 0.64     # AudioSR works best on 5.12 s pieces; longer sounds go in pieces with crossfades

def sr_mono(x, rate):
    """one channel (numpy, original rate) -> 48 kHz, at the level of the input"""
    def one(seg):
        tmp = os.path.join(tmpdir, "seg.wav")
        want = int(round(len(seg) * 48000 / rate))
        if float(np.max(np.abs(seg))) < 1e-4:          # silence: nothing to restore
            return np.zeros(want, np.float32)
        sf.write(tmp, seg, rate, subtype="FLOAT")
        try:
            o = sr_file(tmp)[:want]
        except Exception as e:
            # AudioSR rejects some pieces (very quiet, or with no clear band limit: "Digital filter
            # critical frequencies must be 0 < Wn < 1"); such a piece keeps its original sound
            fallbacks.append(str(e))
            return plain48(seg, rate)[:want]
        pi, po = float(np.max(np.abs(seg))), float(np.max(np.abs(o))) if len(o) else 0.0
        # AudioSR scales every input to the same level: give the piece its own back
        return o * (pi / po) if po > 0 and pi > 0 else o * 0
    n = len(x); step = int((CHUNK - OVER) * rate); size = int(CHUNK * rate)
    if n <= int(10.24 * rate):
        return one(x)
    total = int(round(n * 48000 / rate)); out = np.zeros(total, np.float32); wsum = np.zeros(total, np.float32)
    fade = int(OVER * 48000)
    for st in range(0, n, step):
        seg = x[st: st + size]
        if len(seg) < int(0.5 * rate) and st > 0:
            break
        o = one(seg)
        a = int(round(st * 48000 / rate)); o = o[: total - a]
        w = np.ones(len(o), np.float32)
        if st > 0: w[:fade] = np.linspace(0, 1, min(fade, len(o)), dtype=np.float32)[: len(w[:fade])]
        if st + size < n: w[-fade:] = np.linspace(1, 0, min(fade, len(o)), dtype=np.float32)[-len(w[-fade:]):]
        out[a: a + len(o)] += o * w; wsum[a: a + len(o)] += w
        if st + size >= n:
            break
    return out / np.maximum(wsum, 1e-6)

t0 = time.time(); failed = 0
for i, f in enumerate(todo):
    base = os.path.splitext(os.path.basename(f))[0]
    dst = os.path.join(out, base + ".wav")
    try:
        data, rate = sf.read(f, dtype="float32", always_2d=True)
        chans = [sr_mono(data[:, c], rate) for c in range(data.shape[1])]
        m = min(len(c) for c in chans)
        y = np.stack([c[:m] for c in chans], axis=1)
        part = dst + ".part.wav"
        sf.write(part, y, 48000, subtype="FLOAT")
        os.replace(part, dst)
    except Exception as e:
        failed += 1
        print("  %s: %s" % (base, e), flush=True)
        continue
    el = time.time() - t0
    print("  %d / %d  %s  (%.0f min left)" % (i + 1, len(todo), base, el / (i + 1) * (len(todo) - i - 1) / 60), flush=True)
if fallbacks:
    print("  %d pieces kept their original sound (AudioSR could not process them)" % len(fallbacks), flush=True)
# a sound that failed stays the original one in the game: the run goes on rather than stopping
sys.exit(0)
