import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upkg import Package
from udis import Disasm
pkg = Package(sys.argv[1]); d = Disasm(pkg)
classes = set(sys.argv[2].split(','))
for e in pkg.exports:
    if pkg.classname(e) != 'Function': continue
    fn = pkg.fullname(e['idx'])
    if fn.split('.')[0] not in classes: continue
    info, out, _ = d.disasm(e)
    print(f'== {fn}')
    for off, txt in out: print(f'  {off:04x}: {txt}')
