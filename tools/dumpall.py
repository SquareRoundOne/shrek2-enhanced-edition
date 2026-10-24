import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upkg import Package
from udis import Disasm
pkg = Package(sys.argv[1]); d = Disasm(pkg)
bad = 0
for e in pkg.exports:
    if pkg.classname(e) != 'Function' or e['size'] == 0: continue
    fn = pkg.fullname(e['idx'])
    try:
        info, out, _ = d.disasm(e)
    except Exception as ex:
        print(f'== {fn}\n  !! {ex}'); bad += 1; continue
    print(f'== {fn}')
    for off, txt in out:
        print(f'  {off:04x}: {txt}')
        if txt.startswith('!!'): bad += 1
print('BAD', bad, file=sys.stderr)
