"""KWGame.u: pickups (coins, chocolate bars, health boxes...) no longer shrink and vanish with distance.
KWPickup.CheckForDistance hid a pickup past PickupRadius from the camera and drew it at 20/40/60/80% of its
size in the rings inside - with the long view of the Enhanced Edition the shrinking is plain to see. New, first
in CheckForDistance:
    PickupRadius = 1000000.0;   // every pickup is in the innermost ring: full size, shown
Also (0.9.73): a pickup the map placed smaller than its class (the energy kegs of the hunt, parts 1, 2 and 4, are placed at
DrawScale 0.1, the bar of part 3 at 0.25) used to be resized only when it crossed a distance ring - and now never does -
so after the radius it gets, right away:
    if (default.DrawScale > DrawScale) SetDrawScale(default.DrawScale);
The ambient effects' own radius (AmbientFXRadius) stays as it was.
usage: pickuppatch.py <KWGame.u in> <KWGame.u out>
"""
import os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upkg import Package
from ubc import Func, Node
from upkgw import rebuild
from hudpatch import get_func, Ctx, inst, fconst


def patch(pkg):
    c = Ctx(pkg)
    f = Func(pkg, get_func(pkg, 'KWPickup.CheckForDistance'))
    pr = c.ref('KWPickup.PickupRadius')
    for st in f.stmts:
        if st.t == 0x0F and st.f[0][1].t == 0x01 and st.f[0][1].f[0][1] == pr:
            raise SystemExit('already patched')
    import copy
    ds = c.ref('Actor.DrawScale')
    # the case-5 statement of the switch: SetDrawScale(default.DrawScale * 1.0)
    setst = None
    for st in f.stmts:
        if st.t == 0x1C and st.f[1][0] == 'params':
            a = st.f[1][1][0]
            if a.t == 0xAB and a.f[0][1][1].t == 0x1E and abs(struct.unpack('<f', a.f[0][1][1].f[0][1])[0] - 1.0) < 1e-6: setst = st
    assert setst is not None
    defds = copy.deepcopy(setst.f[1][1][0].f[0][1][0])             # default.DrawScale
    body = copy.deepcopy(setst); body.f[1] = ('params', [copy.deepcopy(defds)])
    first = f.stmts[3]   # after the early return of pickups that do not check distance
    guard = Node(0x07, [('jump', first), ('expr', Node(0xB1, [('params', [copy.deepcopy(defds), inst(ds)])]))])
    f.stmts[3:3] = [guard, body]
    f.stmts.insert(0, Node(0x0F, [('expr', inst(pr)), ('expr', fconst(1000000.0))]))
    body, size = f.emit_checked()
    return rebuild(pkg, {f.e['idx']: body}), f'KWPickup.CheckForDistance: pickups keep their size, {f.script_size} -> {size}'


if __name__ == '__main__':
    b, msg = patch(Package(sys.argv[1]))
    open(sys.argv[2], 'wb').write(b)
    print(msg)
