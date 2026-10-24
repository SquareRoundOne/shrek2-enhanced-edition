"""GUI.u data/script patches."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upkg import Package
from ubc import Func, walk
from upkgw import rebuild
from hudpatch import get_func

def main(src, dst):
    p = Package(src)
    bodies = {}
    # 1. closing a menu must not switch the screen to a stale "GameResolution" (e.g. 800x600):
    #    'SETRES <res>' -> 'GETCURRENTRES <res>' (harmless query) in the two restore paths.
    for fn in ('GUIPage.CheckResolution', 'GUIController.CloseAll'):
        e = get_func(p, fn); f = Func(p, e); n = [0]
        def v(node):
            if node.t == 0x1F and node.f[0][1] == b'SETRES\0':
                node.f[0] = ('zstr', b'GETCURRENTRES\0'); n[0] += 1
            return node
        for s in f.stmts: walk(s, v)
        assert n[0] == 1, (fn, n[0])
        bodies[e['idx']] = f.emit_checked()[0]
        print(fn, 'patched')
    out = bytearray(rebuild(p, bodies))
    # 2. GUIFont.FontArrayResolutions 800,1024,1280,1600 -> 800,1024,99999,99999
    pat = struct.pack('<4i', 800, 1024, 1280, 1600)
    i = out.find(pat); assert i > 0 and out.find(pat, i + 1) < 0
    out[i:i + 16] = struct.pack('<4i', 800, 1024, 99999, 99999)
    open(dst, 'wb').write(out)

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
