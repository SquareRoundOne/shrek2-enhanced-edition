"""HD interface textures: scripts read a texture's size from BitmapMaterial.USize / VSize to size
and place HUD and menu pictures (Canvas.DrawTile(T, T.USize * scale, ..., 0, 0, T.USize, T.VSize)).
An upscaled texture has USize = 4x, while UClamp / VClamp keep the authored size (the patched
Engine.dll already takes canvas UVs from them). Every script read of USize / VSize becomes a read
of UClamp / VClamp - the same value for every texture that was not upscaled.

usage: uclamp.py <package in> <package out>   (Engine.u, KWGame.u, SHGame.u)
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upkg import Package
from ubc import Func, Node, walk
from upkgw import rebuild

SKIP_CLASSES = {'ShadowProjector'}      # its render target keeps USize only


def main(src, dst):
    pkg = Package(src)
    names = {n: i for i, n in enumerate(pkg.names)}

    def find(full):
        for i in range(len(pkg.imports)):
            if pkg.fullname(-i - 1).lower() == full.lower(): return -i - 1
        short = full.split('.', 1)[1].lower()
        for e in pkg.exports:
            fn = pkg.fullname(e['idx']).lower()
            if fn == full.lower() or fn == short: return e['idx']
        return None

    new_imports = []
    def ref(full, base):
        r = find(full)
        if r is not None: return r
        # Core.IntProperty import under Engine.BitmapMaterial
        owner = find('Engine.BitmapMaterial')
        assert owner is not None and owner < 0, 'BitmapMaterial not imported'
        new_imports.append({'class_pkg': 'Core', 'class_name': 'IntProperty', 'package': owner, 'name': base})
        return -(len(pkg.imports) + len(new_imports))

    usize, vsize = find('Engine.BitmapMaterial.USize'), find('Engine.BitmapMaterial.VSize')
    if usize is None and vsize is None:
        print('no USize/VSize reads'); open(dst, 'wb').write(pkg.data); return
    swap = {}
    if usize is not None: swap[usize] = ref('Engine.BitmapMaterial.UClamp', 'UClamp')
    if vsize is not None: swap[vsize] = ref('Engine.BitmapMaterial.VClamp', 'VClamp')

    bodies = {}; total = 0
    for e in pkg.exports:
        if pkg.classname(e) != 'Function' or e['size'] <= 0: continue
        full = pkg.fullname(e['idx'])
        if full.split('.')[0] in SKIP_CLASSES: continue
        try:
            f = Func(pkg, e)
        except Exception:
            continue
        n = [0]
        def visit(node, parent):
            # reads only: the left side of an assignment keeps USize
            if node.t == 0x01 and node.f[0][1] in swap and not (parent is not None and parent.t == 0x0F and parent.f[0][1] is node):
                node.f[0] = ('ref', swap[node.f[0][1]]); n[0] += 1
            for k, v in node.f:
                if k == 'expr' and v is not None: visit(v, node)
                elif k == 'params':
                    for p in v: visit(p, node)
        for s in f.stmts: visit(s, None)
        if n[0]:
            b, _ = f.emit_checked(); bodies[e['idx']] = b; total += n[0]
            print(f'  {full}: {n[0]}')
    out = rebuild(pkg, bodies, new_imports)
    open(dst, 'wb').write(out)
    print(f'{os.path.basename(src)}: {total} reads in {len(bodies)} functions, {len(new_imports)} new imports')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
