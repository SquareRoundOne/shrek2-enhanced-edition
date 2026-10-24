"""KWGame.u (already camfadepatch'ed): no camera-blocking fade in any cutscene.
The first guard (Camera.Controller is a KWCutController) misses cutscenes whose camera is not
driven by a cut controller (the swamp intro). New guard before it:
    if (bInCutScene(true)) return;     // KWCutScene.default.bInCutScene, or the hero in stasis
usage: camfadepatch2.py <KWGame.u in> <KWGame.u out>
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upkg import Package
from ubc import Func, Node
from upkgw import rebuild
from hudpatch import get_func
from potionpatch import if_not, ret

def patch(pkg):
    f = Func(pkg, get_func(pkg, 'KWHeroController.CheckForCameraBlocking'))
    st = f.stmts
    loops = [i for i, s in enumerate(st) if s.t == 0x2F]
    assert len(loops) == 2
    i2 = loops[1]
    g1 = st[i2 - 2]
    assert g1.t == 0x07 and st[i2 - 1].t == 0x04, 'first guard not found'
    assert not (st[i2 - 4].t == 0x07 and st[i2 - 4].f[1][1].t == 0x1B), 'already patched'
    call = Node(0x1B, [('name', pkg.names.index('bInCutScene')), ('params', [Node(0x27)])])
    g2 = if_not(call, g1)
    # the launcher switch (bDoOpacityForCamera) moves here too: with it off, actors faded before
    # still come back to full opacity in the first loop instead of staying see-through
    import copy
    s0 = st[0]
    assert s0.t == 0x07 and st[1].t == 0x04
    g0 = if_not(copy.deepcopy(s0.f[1][1]), g2); g0.f[1][1].old = None
    def clr(x):
        x.old = None
        for k, v in x.f:
            if k == 'expr' and v is not None: clr(v)
            if k == 'params':
                for y in v: clr(y)
    clr(g0.f[1][1])
    s0.f[1] = ('expr', Node(0x28))          # if (!false) ... : the old early return never happens
    def fix(n):
        for j, (k, v) in enumerate(n.f):
            if k == 'jump' and v is g1: n.f[j] = (k, g0)
            if k == 'expr' and v is not None: fix(v)
            if k == 'params':
                for y in v: fix(y)
    for s in st:
        if s is not g2 and s is not g0: fix(s)
    st[i2 - 2:i2 - 2] = [g0, ret(), g2, ret()]
    body, size = f.emit_checked()
    return rebuild(pkg, {f.e['idx']: body}), f'CheckForCameraBlocking: + bInCutScene(true) guard, {f.script_size} -> {size}'

if __name__ == '__main__':
    b, msg = patch(Package(sys.argv[1]))
    open(sys.argv[2], 'wb').write(b); print(msg)
