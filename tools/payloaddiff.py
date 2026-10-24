r"""What our work changed in the game folder: every file of <modified game> that is missing from or differs from <clean original>,
outside System\ (the base package handles System itself) and outside the packs' work folders.
usage: payloaddiff.py <modified game folder> <clean original folder> <out manifest.json>
The manifest lists relative paths (changed / added) and their sizes: the installer's [Files] section is built from it."""
import hashlib, json, os, sys
from concurrent.futures import ThreadPoolExecutor

mod, orig, out = sys.argv[1], sys.argv[2], sys.argv[3]
SKIP_TOP = {'System'}          # the base package installs System; lang packs are added separately


def md5(p):
    h = hashlib.md5()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


files = []
for root, _, fs in os.walk(mod):
    rel_root = os.path.relpath(root, mod)
    if rel_root.split(os.sep)[0] in SKIP_TOP:
        continue
    for f in fs:
        rel = os.path.normpath(os.path.join(rel_root, f)) if rel_root != '.' else f
        files.append(rel)


def check(rel):
    a, b = os.path.join(mod, rel), os.path.join(orig, rel)
    sa = os.path.getsize(a)
    if not os.path.exists(b):
        return rel, sa, 'added'
    if os.path.getsize(b) != sa:
        return rel, sa, 'changed'
    return (rel, sa, 'changed') if md5(a) != md5(b) else None


with ThreadPoolExecutor(8) as ex:
    res = [r for r in ex.map(check, files) if r]
res.sort()
json.dump([{'path': r[0], 'size': r[1], 'kind': r[2]} for r in res], open(out, 'w'), indent=1)
tot = sum(r[1] for r in res)
print(len(files), 'files looked at,', len(res), 'differ or are new,', tot // (1 << 20), 'MB')
by = {}
for r in res:
    k = r[0].split(os.sep)[0]; by.setdefault(k, [0, 0]); by[k][0] += 1; by[k][1] += r[1]
for k, v in sorted(by.items()):
    print(' ', k, v[0], 'files', v[1] // (1 << 20), 'MB')
