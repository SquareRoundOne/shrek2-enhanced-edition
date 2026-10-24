"""Pause-menu book askew when the menu is opened from a gamepad.

The menu page (ShInGameMenuGUIPage.InitComponent) turns the camera to a fixed rotation and puts
the book in front of it. Esc on the keyboard is handled from the window messages, before the
level tick, so the frame is already paused and the camera does not move again. A gamepad button
is read inside PlayerController's tick: the menu opens in the middle of a level tick, and the
camera's own Tick later in that same frame turns it back to the gameplay view - the book ends
up off-centre and tilted for the whole pause.

KWGame.u: every gameplay BaseCam state Tick starts with
    if (Level.Pauser != None) return;
so nothing moves the camera once the game is paused, whatever opened the menu.

usage: pausepatch.py <KWGame.u in> <KWGame.u out>
"""
import copy, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upkg import Package
from ubc import Func, Node
from upkgw import rebuild
from hudpatch import get_func
from skippatch import strip_old, insert_before, if_not, find

STATES = ['StateStandardCam', 'StateFollowCam', 'StateDuelingCam', 'StateFreeCam',
          'StateFlyingCarCam', 'StateQuidditchCam', 'StateTransition', 'StatePlaybackCam']

def patch(pkg, log):
    sm = Func(pkg, get_func(pkg, 'KWHeroController.ShowMenu'))
    i = find(sm.stmts, lambda s: s.t == 0x07 and s.f[1][1].t == 0x72 and 'Pauser' in repr(s) or
             (s.t == 0x07 and s.f[1][1].t == 0x72 and s.f[1][1].f[0][1][1].t == 0x2A))
    cond = sm.stmts[i].f[1][1]                      # Level.Pauser == None
    assert cond.t == 0x72 and cond.f[0][1][1].t == 0x2A, cond
    bodies = {}
    for st_name in STATES:
        f = Func(pkg, get_func(pkg, f'BaseCam.{st_name}.Tick'))
        st = f.stmts
        assert st[-1].t == 0x04, (st_name, st[-1])
        n = insert_before(f, 0, [if_not(strip_old(copy.deepcopy(cond)), st[-1])])
        body, size = f.emit_checked()
        bodies[f.e['idx']] = body
        log.append(f'  BaseCam.{st_name}.Tick: frozen while paused, {f.script_size} -> {size}')
    return rebuild(pkg, bodies)

if __name__ == '__main__':
    log = []
    data = patch(Package(sys.argv[1]), log)
    open(sys.argv[2], 'wb').write(data)
    print('\n'.join(log))
