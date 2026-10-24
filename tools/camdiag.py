"""Diagnostic build: per-frame camera log line at the end of BaseCam.StateStandardCam.Tick."""
import sys, os, copy, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upkg import Package
from ubc import Func, Node, walk
from upkgw import rebuild
from hudpatch import Ctx, get_func, nat, cast, ctx, inst

src, dst = sys.argv[1], sys.argv[2]
pkg = Package(src); c = Ctx(pkg)
e = get_func(pkg, 'BaseCam.Tick')
f = Func(pkg, e)
loc = lambda name: Node(0x00, [('ref', next(x['idx'] for x in pkg.exports if x['package'] == e['idx'] and x['name'] == name))])
S = lambda t: Node(0x1F, [('zstr', t.encode() + b'\0')])
cat = lambda a, b: nat(0x70, a, b)
from hudpatch import fconst
f2s = lambda x, k=100.0: cast(0x53, cast(0x44, nat(0xAB, x, fconst(k))))
i2s = lambda x: cast(0x53, x)
sm = lambda field, x: Node(0x36, [('ref', c.ref(field)), ('expr', x)])
I = lambda n: inst(c.ref(n))
pawn = lambda: ctx(I('BaseCam.Player'), I('Controller.Pawn'))
tgt = lambda: I('BaseCam.CamTarget')
level = lambda: I('Actor.Level')
items = [
    f2s(loc('DeltaTime'), 1e6),
    f2s(ctx(level(), I('LevelInfo.TimeSeconds')), 1000.0),
    i2s(sm('Rotator.Yaw', I('BaseCam.rCurrRotation'))),
    i2s(sm('Rotator.Yaw', I('BaseCam.rDestRotation'))),
    i2s(sm('Rotator.Yaw', ctx(tgt(), I('Actor.Rotation'), 12))),
    i2s(sm('Rotator.Yaw', ctx(pawn(), I('Actor.Rotation'), 12))),
    i2s(sm('Rotator.Yaw', ctx(I('BaseCam.Player'), I('Actor.DesiredRotation'), 12))),
    f2s(sm('Vector.X', I('Actor.Location'))), f2s(sm('Vector.Y', I('Actor.Location'))), f2s(sm('Vector.Z', I('Actor.Location'))),
    f2s(sm('Vector.X', ctx(tgt(), I('Actor.Location'), 12))), f2s(sm('Vector.Y', ctx(tgt(), I('Actor.Location'), 12))), f2s(sm('Vector.Z', ctx(tgt(), I('Actor.Location'), 12))),
    f2s(sm('Vector.X', ctx(pawn(), I('Actor.Location'), 12))), f2s(sm('Vector.Y', ctx(pawn(), I('Actor.Location'), 12))), f2s(sm('Vector.Z', ctx(pawn(), I('Actor.Location'), 12))),
    f2s(I('BaseCam.fCurrLookAtDistance')),
]
txt = S('CAMDIAG')
for it in items:
    txt = cat(cat(txt, S(' ')), it)
log = Node(0xE7, [('params', [txt])])
last = f.stmts[-1]
assert last.t == 0x04, hex(last.t)
f.stmts[-1:-1] = [log]
body, size = f.emit_checked()
open(dst, 'wb').write(rebuild(pkg, {e['idx']: body}))
print('ok', f.script_size, '->', size)
