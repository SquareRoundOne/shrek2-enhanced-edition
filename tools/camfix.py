"""BaseCam: camera yaw follows the player's input of THIS frame.

In sync mode the mouse/arrow input sets Pawn.DesiredRotation (scaled by this frame's DeltaTime), but the
pawn only turns to it during the next frame's physics, and the camera copies the pawn's yaw. So every
frame showed a rotation step scaled by the previous frame's DeltaTime -> uneven steps whenever frame
times vary (visible judder of near objects while turning at high frame rates).

Patch (BaseCam.UpdateRotation, after  rDestRotation.Yaw = CamTarget.Rotation.Yaw):
    if (Player != None) if (Player.Pawn != None) if (CamTarget.aAttachedTo == Player.Pawn)
        rDestRotation.Yaw += ((Player.DesiredRotation.Yaw - Player.Pawn.Rotation.Yaw + 32768) & 65535) - 32768;
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upkg import Package
from ubc import Func, Node
from upkgw import rebuild
from hudpatch import Ctx, get_func, nat, ctx, inst

def patch(pkg):
    c = Ctx(pkg)
    e = get_func(pkg, 'BaseCam.UpdateRotation')
    f = Func(pkg, e)
    I = lambda n: inst(c.ref(n))
    ic = lambda v: Node(0x1D, [('i32', v)])
    none = lambda: Node(0x2A, [])
    sm = lambda field, x: Node(0x36, [('ref', c.ref(field)), ('expr', x)])
    player = lambda: I('BaseCam.Player')
    pawn = lambda: ctx(player(), I('Controller.Pawn'))
    idx = next(i for i, s in enumerate(f.stmts) if s.t == 0x0F and s.f[0][1].t == 0x36
               and pkg.objname(s.f[0][1].f[0][1]) == 'Yaw' and pkg.objname(s.f[0][1].f[1][1].f[0][1]) == 'rDestRotation')
    after = f.stmts[idx + 1]
    dyaw = sm('Rotator.Yaw', ctx(player(), I('Actor.DesiredRotation'), 12))
    pyaw = sm('Rotator.Yaw', ctx(pawn(), I('Actor.Rotation'), 12))
    pending = nat(0x93, nat(0x9C, nat(0x92, nat(0x93, dyaw, pyaw), ic(32768)), ic(65535)), ic(32768))
    lhs = lambda: sm('Rotator.Yaw', I('BaseCam.rDestRotation'))
    assign = Node(0x0F, [('expr', lhs()), ('expr', nat(0x92, lhs(), pending))])
    j1 = Node(0x07, [('jump', after), ('expr', nat(0x77, player(), none()))])
    j2 = Node(0x07, [('jump', after), ('expr', nat(0x77, pawn(), none()))])
    j3 = Node(0x07, [('jump', after), ('expr', nat(0x72, ctx(I('BaseCam.CamTarget'), I('BaseCamTarget.aAttachedTo')), pawn()))])
    f.stmts[idx + 1:idx + 1] = [j1, j2, j3, assign]
    body, size = f.emit_checked()
    return {e['idx']: body}, (f.script_size, size)

if __name__ == '__main__':
    pkg = Package(sys.argv[1])
    bodies, sz = patch(pkg)
    open(sys.argv[2], 'wb').write(rebuild(pkg, bodies))
    print('BaseCam.UpdateRotation', sz)
