"""KWGame.u: the jump key (Jump | BypassCutscene) no longer cuts a line the heroes say during play.
KWHud.BypassCutscene finished the last triggered cutscene whatever it was - also the small dialogue
"cutscenes" that run while the player keeps control, so a jump stopped the line. Now it acts only
while a real cutscene is on (CutTextController.bCutSceneInProgress, false for mini dialogue).
usage: bypasspatch.py <KWGame.u in> <KWGame.u out>
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upkg import Package
from ubc import Func, Node
from upkgw import rebuild
from hudpatch import get_func
from potionpatch import cp, if_not, jump, ret, repr_names

def refs(pkg, n):
    out = []
    def w(x):
        for k, v in x.f:
            if k == 'ref' and v: out.append(pkg.fullname(v))
            if k == 'expr' and v is not None: w(v)
            if k == 'params':
                for y in v: w(y)
    w(n); return ' '.join(out)

def patch(pkg):
    pr = Func(pkg, get_func(pkg, 'KWHud.PostRender'))
    cond = None
    for s in pr.stmts:
        if s.t == 0x07 and s.f[1][1].t == 0x82:
            left = s.f[1][1].f[0][1][0]
            if left.t == 0x19 and 'bCutSceneInProgress' in refs(pkg, left):
                cond = left; break
    assert cond is not None and cond.t == 0x19
    f = Func(pkg, get_func(pkg, 'KWHud.BypassCutscene'))
    st = f.stmts
    assert 'bCutSceneInProgress' not in refs(pkg, st[0]), 'already patched'
    R = ret()
    first = st[0]
    st[0:0] = [if_not(cp(cond), R), jump(first), R]
    body, size = f.emit_checked()
    return rebuild(pkg, {f.e['idx']: body}), f'KWHud.BypassCutscene: only in real cutscenes, {f.script_size} -> {size}'

if __name__ == '__main__':
    b, msg = patch(Package(sys.argv[1]))
    open(sys.argv[2], 'wb').write(b); print(msg)
