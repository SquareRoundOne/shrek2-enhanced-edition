"""KWGame.u: a press of the second jump right at the top of a jump is no longer lost.
KWPawn.CanDoubleJump:  bCanDoubleJump && !bDoingDoubleJump && Velocity.Z > 0
becomes                (the same)  ||  (bCanDoubleJump && !bDoingDoubleJump && Velocity.Z > -60 && Physics == PHYS_Falling)
Every case the original allowed stays allowed (also hanging on a ledge, where the jump pulls the
hero up); the only addition is the first instant of falling after the top of a jump.
usage: jumppatch.py <KWGame.u in> <KWGame.u out>
"""
import os, sys, struct, copy
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upkg import Package
from ubc import Func, Node
from upkgw import rebuild
from hudpatch import get_func
from potionpatch import cp

def patch(pkg):
    f = Func(pkg, get_func(pkg, 'KWPawn.CanDoubleJump'))
    r = f.stmts[0]; top = r.f[0][1]
    assert r.t == 0x04 and top.t == 0x82, 'already patched?'
    left, right = top.f[0][1]
    canAndNot = left                                   # bCanDoubleJump && !bDoingDoubleJump
    zgt = right.f[1][1]                                # Velocity.Z > IntToFloat(0)
    assert zgt.t == 0xB1
    z2 = cp(zgt); a, b = z2.f[0][1]
    z2.f[0] = ('params', [a, Node(0x1E, [('f32', struct.pack('<f', -60.0))])])
    src = Func(pkg, get_func(pkg, 'KWPawn.SetMovementPhysics'))
    phys = cp(next(s for s in src.stmts if s.t == 0x07 and s.f[1][1].t == 0x84).f[1][1].f[0][1][0])   # Physics == 2
    assert phys.t == 0x9A
    alt = Node(0x82, [('params', [Node(0x82, [('params', [cp(canAndNot), Node(0x18, [('skip', None), ('expr', z2)])])]),
                                  Node(0x18, [('skip', None), ('expr', phys)])])])
    r.f[0] = ('expr', Node(0x84, [('params', [top, Node(0x18, [('skip', None), ('expr', alt)])])]))
    body, size = f.emit_checked()
    return rebuild(pkg, {f.e['idx']: body}), f'KWPawn.CanDoubleJump: + top-of-jump grace, {f.script_size} -> {size}'

if __name__ == '__main__':
    b, msg = patch(Package(sys.argv[1]))
    open(sys.argv[2], 'wb').write(b); print(msg)
