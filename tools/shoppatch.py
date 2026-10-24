"""SHGame.u: the potion shop (DriveThroughMenu) laid out whole in the centred 4:3 box.
GUI.dll (guipatch.py) puts top-level components into a centred 4:3 box, except the full-width ones
(WinWidth >= 0.999). The shop's page is full-width, so its picture (the shelves, the frames of the
counts) was stretched over 16:9 while its labels and buttons sat in the 4:3 box: the counts and the
prices missed their frames. New, in DriveThroughMenu.InitComponent right after Super.InitComponent:
    WinWidth = 0.9989;       // just under full width: the page and its picture go into the 4:3 box too
usage: shoppatch.py <SHGame.u in> <SHGame.u out>
"""
import os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upkg import Package
from ubc import Func, Node
from upkgw import rebuild
from hudpatch import get_func, Ctx, inst, fconst


def patch(pkg):
    c = Ctx(pkg)
    f = Func(pkg, get_func(pkg, 'DriveThroughMenu.InitComponent'))
    ww = c.ref('GUIComponent.WinWidth')
    for st in f.stmts:
        if st.t == 0x0F and st.f[0][1].t == 0x01 and st.f[0][1].f[0][1] == ww:
            raise SystemExit('already patched')
    # right after Super.InitComponent (the second statement), so nothing of the parent's set-up overrides it
    at = next(i for i, st in enumerate(f.stmts) if st.t == 0x1C or (st.t not in (0x0F,) and i > 0)) + 1
    f.stmts.insert(at, Node(0x0F, [('expr', inst(ww)), ('expr', fconst(0.9989))]))
    body, size = f.emit_checked()
    return rebuild(pkg, {f.e['idx']: body}), f'DriveThroughMenu.InitComponent: page in the 4:3 box, {f.script_size} -> {size}'


if __name__ == '__main__':
    b, msg = patch(Package(sys.argv[1]))
    open(sys.argv[2], 'wb').write(b)
    print(msg)
