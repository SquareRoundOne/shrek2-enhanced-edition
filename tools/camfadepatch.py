"""KWGame.u: no camera-blocking fade in cutscenes.

KWHeroController.CheckForCameraBlocking makes every actor between the hero and the camera
see-through (Actor.bChangeOpacityForCamera). In cutscenes the camera is the cutscene's, often far
from the hero, and the props in the shot (tables, mugs, furniture) went semi-transparent.
New, before the fading loop (the first loop, which brings faded actors back to full opacity, stays):
    if (Camera.Controller != None && Camera.Controller.IsA('KWCutController')) return;

usage: camfadepatch.py <KWGame.u in> <KWGame.u out>
"""
import copy, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upkg import Package
from ubc import Func, Node
from upkgw import rebuild
from hudpatch import get_func
from potionpatch import cp, if_not, ret


def patch(pkg):
    src = Func(pkg, get_func(pkg, 'KWHeroController.Possess'))
    cond = next(s for s in src.stmts if s.t == 0x07).f[1][1]       # Camera.Controller != None && ...IsA('KWCutController')
    assert cond.t == 0x82
    f = Func(pkg, get_func(pkg, 'KWHeroController.CheckForCameraBlocking'))
    st = f.stmts
    loops = [i for i, s in enumerate(st) if s.t == 0x2F]
    assert len(loops) == 2, 'already patched or unexpected'
    i2 = loops[1]
    r = ret()
    guard = if_not(cp(cond), st[i2])
    # jumps to the second loop (the end of the first one) now land on the guard
    tgt = st[i2].old
    def fix(n):
        for j, (k, v) in enumerate(n.f):
            if k == 'jump' and v == tgt: n.f[j] = (k, guard)
            if k == 'expr' and v is not None: fix(v)
            if k == 'params':
                for y in v: fix(y)
    for s in st: fix(s)
    st[i2:i2] = [guard, r]
    body, size = f.emit_checked()
    return rebuild(pkg, {f.e['idx']: body}), f'KWHeroController.CheckForCameraBlocking: no fading in cutscenes, {f.script_size} -> {size}'


if __name__ == '__main__':
    b, msg = patch(Package(sys.argv[1]))
    open(sys.argv[2], 'wb').write(b)
    print(msg)


def make_config(data, pkg, fullname):
    """turn a BoolProperty into a config one (flag CPF_Config), in place: launcher-settable"""
    e = next(x for x in pkg.exports if pkg.fullname(x['idx']) == fullname)
    o = e['offset']; b = bytearray(data)
    # [None][super ci][next ci][ArrayDim i32][PropertyFlags u32][Category ci]
    from upkg import Reader
    r = Reader(bytes(b), o); r.ci(); r.ci(); r.ci(); r.i32()
    flags = int.from_bytes(b[r.p:r.p + 4], 'little')
    b[r.p:r.p + 4] = (flags | 0x4000).to_bytes(4, 'little')
    return bytes(b)
