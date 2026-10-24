# Apollo (music restoration of lossy audio, https://github.com/JusperLee/Apollo, CC BY-SA 4.0) over the
# game's music: OGG in, OGG out, stereo kept, 44.1 kHz. The model loads once; tracks already in the
# output folder are skipped, so an interrupted run continues where it stopped.
#   python run_apollo.py <music lo dir> <music hi dir>
import os, sys, time, glob, faulthandler
faulthandler.enable()        # a crash inside a library still says where it happened
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, "apollo"))
import numpy as np
import soundfile as sf
import torch
import torchaudio
import inference as ap
import look2hear.models

src, dst = sys.argv[1], sys.argv[2]
os.makedirs(dst, exist_ok=True)
tracks = sorted(glob.glob(os.path.join(src, "*.ogg")))
todo = [t for t in tracks if not os.path.exists(os.path.join(dst, os.path.basename(t)))]
if not todo:
    sys.exit(0)
dev = ap.select_device("auto")
print("Apollo on %s, %d tracks" % (dev, len(todo)), flush=True)
model = look2hear.models.BaseModel.from_pretrain(str(ap.resolve_checkpoint(ap.OFFICIAL_CHECKPOINT)),
                                                 sr=ap.SAMPLE_RATE, win=20, feature_dim=256, layer=6).to(dev).eval()
chunk, overlap, pad = ap.resolve_chunking(6.0, 1.0, 1.0)
t0 = time.time(); failed = 0
for i, t in enumerate(todo):
    name = os.path.basename(t)
    print("  -> %s" % name, flush=True)
    try:
        audio, sr = sf.read(t, dtype="float32", always_2d=True)
        x = torch.from_numpy(np.ascontiguousarray(audio.T)).unsqueeze(0)
        if sr != ap.SAMPLE_RATE:
            x = torchaudio.functional.resample(x, sr, ap.SAMPLE_RATE)
        with torch.inference_mode():
            y = ap.run_model(model, x, dev, chunk_samples=chunk, overlap_samples=overlap, chunk_batch_size=2, chunk_pad_samples=pad)
        y = y[..., : x.shape[-1]].squeeze(0).clamp(-1, 1).numpy().T
        tmp = os.path.join(dst, name + ".part")
        # OGG Vorbis at a high quality (libsndfile: compression 0 = best); the game plays only OGG music
        # written in small blocks: libsndfile's Vorbis encoder crashes the whole process (no Python
        # error) when it gets a long track in a single write
        with sf.SoundFile(tmp, "w", ap.SAMPLE_RATE, y.shape[1], format="OGG", subtype="VORBIS",
                          compression_level=0.1) as f:
            for b in range(0, len(y), 16384):
                f.write(np.ascontiguousarray(y[b: b + 16384]))
        os.replace(tmp, os.path.join(dst, name))
    except Exception as e:
        failed += 1
        print("  %s: %s" % (name, e), flush=True)
        continue
    el = time.time() - t0
    print("  %d / %d  %s  (%.0f s left)" % (i + 1, len(todo), name, el / (i + 1) * (len(todo) - i - 1)), flush=True)
sys.exit(1 if failed == len(todo) else 0)
