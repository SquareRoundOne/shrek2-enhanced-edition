"""Skippable menu-book intro (Book_FrontEnd, Cutscenes\\GameMenu.int).

KWGame.u
  * KWACTION_TargetFlyTo.InitActionFor: when the cutscene is being skipped (C.bDoForceFinish)
    the camera target moves with MoveTime = 0, i.e. it snaps on the next tick instead of
    panning for up to 8 s after the skip.
  * KWACTION_FlyTo.InitActionFor: the skip branch (FancySetLocation(Dest - 25 Z)) is kept for
    characters; a cutscene camera (BaseCam) instead takes the regular path with MoveTime = 0,
    so it ends exactly where the unskipped cutscene ends.
SHGame.u
  * ShrekController.ShowInGameMenu: Esc in SH2_Preamble / Book_FrontEnd sends BypassCutscene
    (it did nothing there before). Space already does that through User.ini.

usage: skippatch.py <dir with KWGame.u SHGame.u> <outdir>
"""
import copy, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upkg import Package
from ubc import Func, Node, walk
from upkgw import rebuild
from hudpatch import get_func, fconst


def strip_old(n):
    def f(x):
        x.old = None
        return x
    return walk(n, f)


def retarget(stmts, old, new_node):
    cnt = 0
    def f(x):
        nonlocal cnt
        for i, (k, v) in enumerate(x.f):
            if k == 'jump' and v == old:
                x.f[i] = (k, new_node); cnt += 1
        return x
    for s in stmts:
        walk(s, f)
    return cnt


def insert_before(f, idx, new):
    tgt = f.stmts[idx]
    n = retarget(f.stmts, tgt.old, new[0])
    f.stmts[idx:idx] = new
    return n


def if_not(cond, target):
    return Node(0x07, [('jump', target), ('expr', cond)])


def assign(lhs, rhs):
    return Node(0x0F, [('expr', lhs), ('expr', rhs)])


def find(stmts, pred):
    for i, s in enumerate(stmts):
        if pred(s):
            return i
    raise LookupError


def is_doflyto(s):
    return s.t == 0x19 and s.f[3][1].t == 0x1B


def patch_kw(pkg, log):
    bodies = {}
    # ---- FlyTo
    f = Func(pkg, get_func(pkg, 'KWACTION_FlyTo.InitActionFor'))
    st = f.stmts
    i_ff = find(st, lambda s: s.t == 0x07 and s.f[1][1].t == 0x19 and s.f[1][1].f[3][1].t == 0x2D)
    ff_cond = st[i_ff].f[1][1]                      # C.bDoForceFinish
    i_pawn = find(st, lambda s: s.t == 0x07 and s.f[1][1].t == 0x77)
    c_pawn = st[i_pawn].f[1][1].f[0][1][0]          # C.Pawn
    movetime = st[find(st, lambda s: s.t == 0x0F and s.f[0][1].t == 0x01 and s.f[1][1].t == 0xAC)].f[0][1]
    # BaseCam class ref from TargetFlyTo
    g = Func(pkg, get_func(pkg, 'KWACTION_TargetFlyTo.InitActionFor'))
    cast = g.stmts[find(g.stmts, lambda s: s.t == 0x07 and s.f[1][1].t == 0x72 and s.f[1][1].f[0][1][0].t == 0x2E)].f[1][1].f[0][1][0]
    assert cast.t == 0x2E
    basecam = cast.f[0][1]
    not_cam = Node(0x72, [('params', [Node(0x2E, [('ref', basecam), ('expr', strip_old(copy.deepcopy(c_pawn)))]),
                                      Node(0x2A)])])
    st[i_ff].f[1] = ('expr', Node(0x82, [('params', [ff_cond, Node(0x18, [('skip', None), ('expr', not_cam)])])]))
    tgt_old = st[i_ff].f[0][1]
    i_norm = find(st, lambda s: s.old == tgt_old)
    norm = st[i_norm]
    new = [if_not(strip_old(copy.deepcopy(ff_cond)), norm),
           assign(strip_old(copy.deepcopy(movetime)), fconst(0.0))]
    n = insert_before(f, i_norm, new)
    body, size = f.emit_checked()
    bodies[f.e['idx']] = body
    log.append(f'  KWACTION_FlyTo.InitActionFor: camera snap on skip ({n} jump retargeted), {f.script_size} -> {size}')

    # ---- TargetFlyTo
    f = g
    st = f.stmts
    c_local = cast.f[1][1].f[0][1]                  # local C (this function)
    ff = strip_old(copy.deepcopy(ff_cond))
    ff.f[0] = ('expr', strip_old(copy.deepcopy(c_local)))
    mt = st[find(st, lambda s: s.t == 0x0F and s.f[0][1].t == 0x01)].f[0][1]
    i_fly = find(st, is_doflyto)
    new = [if_not(ff, st[i_fly]), assign(strip_old(copy.deepcopy(mt)), fconst(0.0))]
    n = insert_before(f, i_fly, new)
    body, size = f.emit_checked()
    bodies[f.e['idx']] = body
    log.append(f'  KWACTION_TargetFlyTo.InitActionFor: target snap on skip ({n} jump retargeted), {f.script_size} -> {size}')

    # ---- GotoMenu: a skipped cutscene leaves the cut-text controller in 'Hourglass' (full-screen
    # black borders) until the cutscene ends; the menu cutscene never ends, so slide the borders out.
    #   if (C.bDoForceFinish) KWHud(CP.myHUD).CutTextController.SlideBordersOut();
    f = Func(pkg, get_func(pkg, 'KWAction_GotoMenu.InitActionFor'))
    st = f.stmts
    i_open = find(st, lambda s: s.t == 0x19 and s.f[3][1].t == 0x1B and pkg.names[s.f[3][1].f[0][1]] == 'ClientOpenMenu')
    cp_local = st[i_open].f[0][1]
    it = st[find(st, lambda s: s.t == 0x2F)]
    c_local2 = it.f[0][1].f[0][1]
    ffc = Func(pkg, get_func(pkg, 'KWCutScene.ForceFinish'))
    show = strip_old(copy.deepcopy(ffc.stmts[find(ffc.stmts, lambda s: s.t == 0x19 and s.f[3][1].t == 0x1B
                                                   and pkg.names[s.f[3][1].f[0][1]] == 'ShowBypass')]))
    show.f[3][1].f[0] = ('name', pkg.names.index('SlideBordersOut'))
    hudcast = show.f[0][1].f[0][1]                  # KWHud(<pc>.myHUD)
    assert hudcast.t == 0x2E
    hudcast.f[1][1].f[0] = ('expr', strip_old(copy.deepcopy(cp_local)))   # <pc> := CP
    ff = strip_old(copy.deepcopy(ff_cond))
    ff.f[0] = ('expr', strip_old(copy.deepcopy(c_local2)))
    # ... and KWHud(CP.myHUD).bHideHUD = true  (SlideOut clears bCutSceneInProgress, which would
    # otherwise bring the gameplay HUD up over the menu book)
    from hudpatch import Ctx
    hide = Node(0x14, [('expr', Node(0x19, [('expr', strip_old(copy.deepcopy(hudcast))), ('skip', None), ('u8', 4),
                                            ('expr', Node(0x2D, [('expr', Node(0x01, [('ref', Ctx(pkg).ref('HUD.bHideHUD'))]))]))])),
                       ('expr', Node(0x27))])
    new = [if_not(ff, st[i_open]), show, hide]
    n = insert_before(f, i_open, new)
    body, size = f.emit_checked()
    bodies[f.e['idx']] = body
    log.append(f'  KWAction_GotoMenu.InitActionFor: borders slide out after a skip ({n} jump retargeted), {f.script_size} -> {size}')

    # ---- KWHud.BypassCutscene: the FOV reset (added by hudpatch) only when a cutscene is really
    # running. The "modern" layout sends BypassCutscene with every jump on Space; without the gate
    # each jump would snap a gameplay FOV change (zoom, FOV volumes) back to the default.
    #   if (KWCutScene.static.GetLastTriggeredCutScene() != None &&
    #       KWCutScene.static.GetLastTriggeredCutScene().IsInState('Running')) { FOV = ...; }
    cf = Func(pkg, get_func(pkg, 'CutFactory.BypassCutscene'))
    getlast = cf.stmts[0].f[1][1]                   # obj:KWCutScene.static.GetLastTriggeredCutScene()
    assert getlast.t == 0x12
    f = Func(pkg, get_func(pkg, 'KWHud.BypassCutscene'))
    st = f.stmts
    instate = st[0].f[1][1]                         # <actor>.IsInState('statePlayerInMovie')
    assert instate.t == 0x19 and instate.f[3][1].t == 0x61
    isrun = strip_old(copy.deepcopy(instate))
    isrun.f[0] = ('expr', strip_old(copy.deepcopy(getlast)))
    isrun.f[3][1].f[1][1][0].f[0] = ('name', pkg.names.index('Running'))
    notnone = Node(0x77, [('params', [strip_old(copy.deepcopy(getlast)), Node(0x2A)])])
    cond = Node(0x82, [('params', [notnone, Node(0x18, [('skip', None), ('expr', isrun)])])])
    i_fov = find(st, lambda s: s.t == 0x0F)
    assert st[-1].t == 0x04
    n = insert_before(f, i_fov, [if_not(cond, st[-1])])
    body, size = f.emit_checked()
    bodies[f.e['idx']] = body
    log.append(f'  KWHud.BypassCutscene: FOV reset only while a cutscene runs, {f.script_size} -> {size}')
    return rebuild(pkg, bodies)


def patch_sh(pkg, log):
    f = Func(pkg, get_func(pkg, 'ShrekController.ShowInGameMenu'))
    st = f.stmts
    i_menu = find(st, lambda s: s.t == 0x07 and any(
        k == 'params' for k, _ in s.f[1][1].f) and b'Book_FrontEnd' in repr(s).encode())
    assert st[i_menu + 1].t == 0x04
    i_byp = find(st, lambda s: s.t == 0x0E and b'BypassCutscene' in repr(s).encode())
    st.insert(i_menu + 1, strip_old(copy.deepcopy(st[i_byp])))
    body, size = f.emit_checked()
    log.append(f'  ShrekController.ShowInGameMenu: Esc skips the menu intro, {f.script_size} -> {size}')
    return rebuild(pkg, {f.e['idx']: body})


if __name__ == '__main__':
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    log = []
    open(os.path.join(dst, 'KWGame.u'), 'wb').write(patch_kw(Package(os.path.join(src, 'KWGame.u')), log))
    open(os.path.join(dst, 'SHGame.u'), 'wb').write(patch_sh(Package(os.path.join(src, 'SHGame.u')), log))
    print('\n'.join(log))
