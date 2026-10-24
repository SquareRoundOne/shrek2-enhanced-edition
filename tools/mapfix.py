"""Map fixes of Shrek 2 Enhanced Edition: distance fog on the beanstalk bonus levels, lighting of
furniture in the potion factory. usage: mapfix.py <MapsDir> <OutDir>"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mappatch import MapPatch
from uprops import read_props

# map -> (fog colour r,g,b, start, end)
FOG = {
    'Beanstalk_bonus':        ((170, 200, 228), 2000.0, 9000.0),   # day: the light blue of the cloud dome
    'Beanstalk_bonus_dawn':   ((126, 130, 136),  600.0, 6500.0),   # rain: grey sky
    'Beanstalk_bonus_knight': (( 10,  12,  42),  800.0, 6000.0),   # night: the dark blue of the night sky
}
# static meshes drawn unlit by mistake (they stay bright in the dark factory)
UNLIT_FIX = {'locker_bench', 'table_rectangle', 'pop_machine', 'container1'}
FACTORY = ['4_FGM_PIB', '5_FGM_Donkey']


def live(m, cls):
    out = []
    for e in m.pkg.exports:
        if m.pkg.classname(e) != cls: continue
        ps, _ = read_props(m.pkg, e)
        if any(p.name == 'bDeleteMe' and p.value for p in ps): continue
        out.append(e)
    return out


def main(src, dst):
    for name, (col, start, end) in FOG.items():
        m = MapPatch(os.path.join(src, name + '.unr'))
        zones = live(m, 'ZoneInfo') + live(m, 'LevelInfo')
        for z in zones:
            for pre in ('', 'HW'):
                m.set(z, f'b{pre}DistanceFog', True, 'bool')
                m.set(z, f'{pre}DistanceFogColor', col + (0,), 'color')
                m.set(z, f'{pre}DistanceFogStart', start, 'float')
                m.set(z, f'{pre}DistanceFogEnd', end, 'float')
        m.save(os.path.join(dst, name + '.unr'))
        print(f'{name}: fog {col} {start:.0f}..{end:.0f} in {", ".join(z["name"] for z in zones)}')
    for name in FACTORY:
        m = MapPatch(os.path.join(src, name + '.unr')); n = 0
        for e in m.pkg.exports:
            if m.pkg.classname(e) != 'StaticMeshActor': continue
            ps, _ = read_props(m.pkg, e); d = {p.name: p.value for p in ps}
            if d.get('bUnlit') and str(d.get('StaticMesh', '')).split('.')[-1] in UNLIT_FIX:
                # without precomputed lighting a lit mesh would only get the (black) zone ambient:
                # a glow of its own keeps it as bright as the lit furniture around it (lockers: 100)
                m.set(e, 'bUnlit', False, 'bool'); m.set(e, 'AmbientGlow', 110, 'byte'); n += 1
        m.save(os.path.join(dst, name + '.unr'))
        print(f'{name}: {n} static meshes lit')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
