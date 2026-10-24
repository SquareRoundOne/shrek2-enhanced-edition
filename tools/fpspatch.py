"""Frame-rate independence for Shrek 2 scripts (KWGame.u, SHGame.u).

The engine itself runs on real frame time, but a few script spots work per frame:

KWGame.u  BaseCam
  * ApplyMouseXToDestYaw / ApplyMouseYToDestPitch: the look input of one frame is clamped to
    fMaxMouseDeltaX/Y (190 / 65). With the mouse a single sensor count already reaches the limit,
    so turning speed was "limit x frames per second" - twice as fast at 144 FPS as at 60, and
    jumpy under a varying frame rate (G-Sync). The limit is now per second:
    limit * DeltaTime * 144 (the feel the remaster was tuned with at 144 Hz, now at any FPS).
  * camera smoothing: every "approach the target" step used  a = FMin(1, k * DeltaTime), an Euler
    step whose result changes with the frame rate (and overshoots at low FPS). Replaced by the
    exact exponential  a = 1 - Exp(-k * DeltaTime): the same motion at 30, 60 or 144 FPS.
SHGame.u
  * tumbling pick-ups and thrown objects (PotionObject, HoleInGroundItems, PotionFeathers,
    PotionCards, CatapultAmmo) and the falling coward peasant turned by a fixed angle per frame;
    now angle * DeltaTime * 60, i.e. the speed they had at 60 FPS.

usage: fpspatch.py <KWGame.u> <SHGame.u> <outdir>
"""
import copy, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upkg import Package
from ubc import Func, Node, walk
from upkgw import rebuild
from hudpatch import get_func, fconst

F_MUL, F_SUB, NEG_F, EXP, FMIN = 0xAB, 0xAF, 0xA9, 0xBF, 0xF4
CAST, LOCAL, FCONST, INTCONST, BYTECONST = 0x39, 0x00, 0x1E, 0x1D, 0x2C
FLOAT2INT, INT2FLOAT = 0x44, 0x3F
CAM_REF, OBJ_REF = 144.0, 60.0


def fmul(a, b):
    return Node(F_MUL, [('params', [a, b])])


def dt_scale(dt_ref, ref):
    return fmul(Node(LOCAL, [('ref', dt_ref)]), fconst(ref))


def param_ref(pkg, e, names=None):
    kids = [x for x in pkg.exports if x.get('package') == e['idx'] and pkg.classname(x) == 'FloatProperty']
    if names:
        kids = [x for x in kids if pkg.objname(x['idx']) in names] or kids
    assert kids, pkg.fullname(e['idx'])
    return kids[0]['idx']


def patch_kw(pkg, log):
    bodies = {}
    # ---- look clamps per second
    for fname, member in (('BaseCam.ApplyMouseXToDestYaw', 'fMaxMouseDeltaX'), ('BaseCam.ApplyMouseYToDestPitch', 'fMaxMouseDeltaY')):
        e = get_func(pkg, fname); f = Func(pkg, e)
        dt = param_ref(pkg, e, ('fTimeDelta',))
        mref = [x['idx'] for x in pkg.exports if pkg.objname(x['idx']) == member]
        assert mref, member
        n = 0
        def fix(x):
            nonlocal n
            if x.t == 0x36 and x.f[0][1] in mref:
                n += 1
                return fmul(x, dt_scale(dt, CAM_REF))
            return x
        for i, s in enumerate(f.stmts):
            f.stmts[i] = walk(s, fix)
        assert n == 4, (fname, n)
        body, size = f.emit_checked(); bodies[e['idx']] = body
        log.append(f'  {fname}: {member} per second ({n}x), {f.script_size} -> {size}')
    # ---- exact exponential smoothing
    for fname in ('BaseCam.UpdatePosition', 'BaseCam.UpdateRotation', 'BaseCam.StateFreeCam.Tick',
                  'BaseCam.UpdateRotationUsingVectors', 'BaseCam.UpdateDistance'):
        e = get_func(pkg, fname); f = Func(pkg, e)
        n = 0
        def fix(x):
            nonlocal n
            if x.t == FMIN:
                a, b = x.f[0][1]
                if a.t == FCONST and struct.unpack('<f', a.f[0][1])[0] == 1.0 and b.t == F_MUL and b.f[0][1][1].t == LOCAL:
                    n += 1
                    k_dt = b
                    return Node(F_SUB, [('params', [fconst(1.0), Node(EXP, [('params', [Node(NEG_F, [('params', [k_dt])])])])])])
            return x
        for i, s in enumerate(f.stmts):
            f.stmts[i] = walk(s, fix)
        assert n >= 1, (fname, n)
        body, size = f.emit_checked(); bodies[e['idx']] = body
        log.append(f'  {fname}: 1 - Exp(-k dt) ({n}x), {f.script_size} -> {size}')
    return rebuild(pkg, bodies)


def patch_sh(pkg, log):
    bodies = {}
    # ---- constant angle per frame -> angle * dt * 60
    for fname in ('PotionObject.Tick', 'HoleInGroundItems.Tick', 'PotionFeathers.Tick', 'PotionCards.Tick'):
        e = get_func(pkg, fname); f = Func(pkg, e)
        dt = param_ref(pkg, e)
        n = 0
        def fix(x):
            nonlocal n
            if x.t in (0xA1, 0xA2):                      # int+= / int-=
                lhs, rhs = x.f[0][1]
                c = None
                if rhs.t == INTCONST: c = rhs.f[0][1]
                elif rhs.t == BYTECONST: c = rhs.f[0][1]
                if c:
                    n += 1
                    rhs2 = Node(CAST, [('u8', FLOAT2INT), ('expr', fmul(fconst(float(c)), dt_scale(dt, OBJ_REF)))])
                    return Node(x.t, [('params', [lhs, rhs2])])
            return x
        for i, s in enumerate(f.stmts):
            f.stmts[i] = walk(s, fix)
        assert n >= 6, (fname, n)
        body, size = f.emit_checked(); bodies[e['idx']] = body
        log.append(f'  {fname}: tumble per second ({n} steps), {f.script_size} -> {size}')
    # CatapultAmmo: CurrentRot = CurrentRot + UpdateRot  ->  + UpdateRot * (dt * 60)
    e = get_func(pkg, 'CatapultAmmo.Flying.Tick'); f = Func(pkg, e); dt = param_ref(pkg, e)
    s0 = f.stmts[0]; add = s0.f[1][1]
    assert add.t == 0x61 and add.f[0][1] == 60, add          # native 316: rotator + rotator
    rot, upd = add.f[1][1]
    add.f[1] = ('params', [rot, Node(0x61, [('u8', 31), ('params', [upd, dt_scale(dt, OBJ_REF)])])])   # native 287: rotator * float
    body, size = f.emit_checked(); bodies[e['idx']] = body
    log.append(f'  CatapultAmmo.Flying.Tick: spin per second, {f.script_size} -> {size}')
    # coward peasant falling over: Pitch += FloatToInt(pitchX)  ->  FloatToInt(pitchX * dt * 60)
    e = get_func(pkg, 'PeasantCowardCombatController.FallOver.Tick'); f = Func(pkg, e); dt = param_ref(pkg, e)
    n = 0
    def fix(x):
        nonlocal n
        if x.t == 0xA1:
            lhs, rhs = x.f[0][1]
            if rhs.t == CAST and rhs.f[0][1] == FLOAT2INT:
                n += 1
                return Node(0xA1, [('params', [lhs, Node(CAST, [('u8', FLOAT2INT), ('expr', fmul(rhs.f[1][1], dt_scale(dt, OBJ_REF)))])])])
        return x
    for i, s in enumerate(f.stmts):
        f.stmts[i] = walk(s, fix)
    assert n == 3, n
    body, size = f.emit_checked(); bodies[e['idx']] = body
    log.append(f'  PeasantCowardCombatController.FallOver.Tick: fall per second, {f.script_size} -> {size}')
    return rebuild(pkg, bodies)


if __name__ == '__main__':
    kw, sh, out = sys.argv[1:4]
    os.makedirs(out, exist_ok=True)
    log = []
    open(os.path.join(out, 'KWGame.u'), 'wb').write(patch_kw(Package(kw), log))
    open(os.path.join(out, 'SHGame.u'), 'wb').write(patch_sh(Package(sh), log))
    print('\n'.join(log))
