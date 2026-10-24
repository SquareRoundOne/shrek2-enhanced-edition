"""SHGame.u: Mongo's camera shake from his footsteps 25% weaker.
Mongo.ShakeTheGround called SimpleShakeView(0.5, 50.0, 0.5); the magnitude is now 37.5.
usage: mongopatch.py <SHGame.u in> <SHGame.u out>
"""
import os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upkg import Package
from ubc import Func, Node
from upkgw import rebuild
from hudpatch import get_func, FCONST


def walk(n, fn):
    if isinstance(n, Node):
        fn(n)
        for _, v in n.f: walk(v, fn)
    elif isinstance(n, (list, tuple)):
        for x in n: walk(x, fn)


def patch(pkg):
    f = Func(pkg, get_func(pkg, 'Mongo.ShakeTheGround'))
    hit = []
    def visit(n):
        if n.t == FCONST and abs(struct.unpack('<f', n.f[0][1])[0] - 50.0) < 1e-6: hit.append(n)
    for st in f.stmts: walk(st, visit)
    if len(hit) != 1: raise SystemExit(f'expected one 50.0 constant, found {len(hit)} (already patched?)')
    hit[0].f[0] = ('f32', struct.pack('<f', 37.5))
    body, size = f.emit_checked()
    return rebuild(pkg, {f.e['idx']: body}), f'Mongo.ShakeTheGround: footstep shake 50 -> 37.5, {f.script_size} -> {size}'


if __name__ == '__main__':
    b, msg = patch(Package(sys.argv[1]))
    open(sys.argv[2], 'wb').write(b)
    print(msg)
