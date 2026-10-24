"""Menu fixes (SHGame.u).

1. The main-menu book (SHMenuBook) is drawn with Canvas.DrawActor without DisplayFOV, so the
   engine takes the player's FOV: with a wider gameplay FOV (95/105) the book shrinks while the
   2D buttons stay where they were - words slide onto the page borders. DrawActor now gets the
   authors' 85.
2. The pause-menu book (GUI.MenuBook, GUI.u) is drawn the same way: its PerformDraw gets 85 too
   (guimenu() below). The main-menu camera itself gets "FOV Angle=85" in Cutscenes GameMenu.int.
3. Save-slot pictures: ShHeroController.SetSaveGameImage wrote the slot's picture into the
   controller and SaveConfig'ed it, but the class default kept the start-up value. Every new
   controller (next level, the menu) started from that stale default: the menu showed only the
   slot number, and the next save wrote the stale value back over the other slots' pictures.
   The default is now updated together with the instance.

usage: menupatch.py <SHGame.u in> <SHGame.u out>
"""
import copy, os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upkg import Package
from ubc import Func, Node
from upkgw import rebuild
from hudpatch import get_func, fconst
from skippatch import insert_before, find, assign

MENU_FOV = 85.0

def ctx(obj, member, size):
    return Node(0x19, [('expr', obj), ('skip', [0, -1]), ('u8', size), ('expr', member)])

def patch(pkg, log):
    bodies = {}
    def imp(full):
        for i in range(len(pkg.imports)):
            if pkg.fullname(-i - 1) == full: return -i - 1
        raise KeyError(full)
    fov = imp('Engine.Controller.FovAngle'); desired = imp('Engine.PlayerController.DesiredFOV')
    # 1
    f = Func(pkg, get_func(pkg, 'SHMenuBook.PerformDraw'))
    call = f.stmts[0].f[3][1]
    assert call.t == 0x61 and call.f[0][1] == 0xD3 and len(call.f[1][1]) == 3
    call.f[1][1].append(fconst(MENU_FOV))
    b, n = f.emit_checked(); bodies[f.e['idx']] = b
    log.append(f'  SHMenuBook.PerformDraw: DrawActor(..., {MENU_FOV})')
    # 3
    f = Func(pkg, get_func(pkg, 'ShHeroController.SetSaveGameImage'))
    cnt = 0
    i = 0
    while i < len(f.stmts):
        s = f.stmts[i]
        if s.t == 0x0F and s.f[0][1].t == 0x01 and pkg.objname(s.f[0][1].f[0][1]).startswith('Save') and pkg.objname(s.f[0][1].f[0][1]).endswith('Image'):
            new = assign(Node(0x02, [('ref', s.f[0][1].f[0][1])]), copy.deepcopy(s.f[1][1]))
            def clr(n):
                n.old = None
                for e in n.exprs(): clr(e)
            clr(new)
            insert_before(f, i + 1, [new]); cnt += 1; i += 1
        i += 1
    assert cnt == 6, cnt
    b, n = f.emit_checked(); bodies[f.e['idx']] = b
    log.append(f'  ShHeroController.SetSaveGameImage: default.SaveNImage updated too ({cnt})')
    # 4. bonus level started directly (launcher "Levels"): no hero label travelled in, and
    #    Shrek.PostLoadGame returned before giving the hero back its 'MainPlayer' tag (it had just
    #    renamed it), so the intro cutscene and the camera lost the player. Now only the switch to
    #    another hero is skipped.
    f = Func(pkg, get_func(pkg, 'Shrek.PostLoadGame'))
    st = f.stmts
    i = find(st, lambda x: x.t == 0x07 and x.f[1][1].t == 0x7A)          # if (FuturePlayerLabel == "")
    assert st[i + 1].t == 0x04 and st[i + 2].t == 0x1B, (st[i + 1], st[i + 2])
    tgt = st[i + 4]                                                     # SavedTag = PlayerHeroActor.Tag
    assert tgt.t == 0x0F, tgt
    st[i + 1] = Node(0x06, [('jump', tgt)], st[i + 1].old)
    b, n = f.emit_checked(); bodies[f.e['idx']] = b
    log.append('  Shrek.PostLoadGame: bonus level without a travelled hero keeps the MainPlayer tag')
    # 5. potion menu: with no potion selected (PotionSelected = 9, e.g. in the bonus levels) the
    #    highlight frame was drawn after the ninth slot, an empty white square.
    f = Func(pkg, get_func(pkg, 'SHPotionSelectMenu.Internal_OnDraw'))
    st = f.stmts
    pot = st[0].f[0][1]                                                  # local:pot
    loop = st[find(st, lambda x: x.t == 0x07 and x.f[1][1].t == 0x96)]  # if (i < 9)
    cond = copy.deepcopy(loop.f[1][1]); cond.f[0][1][0] = copy.deepcopy(pot)
    def clr(n):
        n.old = None
        for e in n.exprs(): clr(e)
    clr(cond)
    i_font = find(st, lambda x: x.t == 0x0F and x.f[0][1].t == 0x19 and 'Font' == pkg.objname(x.f[0][1].f[3][1].f[0][1]))
    st.insert(1, Node(0x07, [('jump', st[i_font]), ('expr', cond)]))
    b, n = f.emit_checked(); bodies[f.e['idx']] = b
    log.append('  SHPotionSelectMenu.Internal_OnDraw: no highlight frame without a selected potion')
    return rebuild(pkg, bodies)

def guimenu(pkg, log):
    f = Func(pkg, get_func(pkg, 'MenuBook.PerformDraw'))
    call = f.stmts[0].f[3][1]
    assert call.t == 0x61 and call.f[0][1] == 0xD3 and len(call.f[1][1]) == 3
    call.f[1][1].append(fconst(MENU_FOV))
    b, n = f.emit_checked()
    log.append(f'  GUI MenuBook.PerformDraw: DrawActor(..., {MENU_FOV})')
    return rebuild(pkg, {f.e['idx']: b})

if __name__ == '__main__' and sys.argv[1].lower().endswith('gui.u'):
    log = []
    data = guimenu(Package(sys.argv[1]), log)
    open(sys.argv[2], 'wb').write(data)
    print('\n'.join(log)); sys.exit()
if __name__ == '__main__':
    log = []
    data = patch(Package(sys.argv[1]), log)
    open(sys.argv[2], 'wb').write(data)
    print('\n'.join(log))
