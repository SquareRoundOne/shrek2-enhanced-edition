"""Potion selection apart from potion use (SHGame.u, ShHeroController.SelectPotion).

The game's exec SelectPotion N picks potion N and uses it at once. Three codes are added; the
numbers 0..8 keep working as before:
  SelectPotion 100  next potion (only selected: the HUD icon changes; every potion in turn, also
                    the ones the hero has none of - the HUD shows those greyed out)
  SelectPotion 101  previous potion
  SelectPotion 102  use the selected potion (with all of the game's checks)

New code after the game's own checks (loading, first 5 s, cutscene):
    if (H == 100) { H = 1;  goto Cycle; }
    if (H == 101) { H = 8;
  Cycle:
        if (bPotionInUse) return;
        if (!(PotionSelected < 9)) return;           // 9: potions off (coin mini game)
        PotionSelected = (PotionSelected + H) mod 9;
        return;
    }
    if (H == 102) H = PotionSelected;
    ... the game's code (CanUsePotion, potion in use, DoIHaveThisPotion(H), use) ...

usage: potionpatch.py <SHGame.u in> <SHGame.u out>
"""
import copy, os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upkg import Package
from ubc import Func, Node
from upkgw import rebuild
from hudpatch import get_func

I_MUL, I_DIV, I_ADD, I_SUB, I_LT, I_GT, I_EQ = 0x90, 0x91, 0x92, 0x93, 0x96, 0x97, 0x9A


def ic(v): return Node(0x1D, [('i32', v)])
def op(t, a, b): return Node(t, [('params', [a, b])])
def let(a, b): return Node(0x0F, [('expr', a), ('expr', b)])
def if_not(cond, tgt): return Node(0x07, [('jump', tgt), ('expr', cond)])
def jump(tgt): return Node(0x06, [('jump', tgt)])
def ret(): return Node(0x04, [('expr', Node(0x0B))])
def log(text, val):   # Log("EE potion: <text>" $ IntToString(val)) -> System\\*.log of the game
    return Node(0xE7, [('params', [Node(0x70, [('params', [Node(0x1F, [('zstr', ('EE potion: ' + text).encode('latin-1') + b'\0')]),
                                                          Node(0x39, [('u8', 0x53), ('expr', val)])])])])])
def cp(n):
    n = copy.deepcopy(n)
    def clr(x):
        x.old = None
        for k, v in x.f:
            if k == 'expr' and v is not None: clr(v)
            if k == 'params':
                for y in v: clr(y)
    clr(n); return n


def patch(pkg):
    f = Func(pkg, get_func(pkg, 'ShHeroController.SelectPotion'))
    st = f.stmts
    if any(s.t == 0x07 and s.f[1][1].t == I_EQ for s in st):
        raise SystemExit('SelectPotion is already patched')
    # pieces of the game's own code
    i_can = next(i for i, s in enumerate(st) if s.t == 0x07 and 'CanUsePotion' in repr_names(pkg, s))
    i_inuse = next(i for i, s in enumerate(st) if s.t == 0x07 and s.f[1][1].t == 0x81 and s.f[1][1].f[0][1][0].t == 0x2D)
    inuse_bool = st[i_inuse].f[1][1].f[0][1][0]            # BoolVariable(self.bPotionInUse)
    i_have = i_inuse + 1
    have_call = st[i_have].f[1][1].f[0][1][0]              # DoIHaveThisPotion(H)
    assert have_call.t == 0x1B
    i_set = next(i for i, s in enumerate(st) if s.t == 0x0F)
    sel = st[i_set].f[0][1]                                # self.PotionSelected
    H = st[i_set].f[1][1]                                  # local H
    assert sel.t == 0x01 and H.t == 0x00

    R = ret()
    canuse = st[i_can]
    first = log('command ', cp(H))
    t_102 = if_not(op(I_EQ, cp(H), ic(102)), canuse)
    R_log = log('unchanged, selected ', cp(sel))
    loop = if_not(op(I_LT, cp(H), ic(90)), R_log)
    have_sel = cp(have_call); have_sel.f[1] = ('params', [cp(sel)])
    t_101 = if_not(op(I_EQ, cp(H), ic(101)), t_102)
    cyc = if_not(Node(0x81, [('params', [cp(inuse_bool)])]), R_log)
    new = [
        first,
        if_not(op(I_EQ, cp(H), ic(100)), t_101),
        let(cp(H), ic(1)),
        jump(cyc),
        t_101,
        let(cp(H), ic(8)),
        cyc,
        if_not(op(I_LT, cp(sel), ic(9)), R_log),
        loop,
        let(cp(sel), op(I_ADD, cp(sel), op(I_SUB, cp(H), op(I_MUL, op(I_DIV, cp(H), ic(10)), ic(10))))),
    ]
    skip = let(cp(H), op(I_ADD, cp(H), ic(10)))
    new += [
        if_not(op(I_GT, cp(sel), ic(8)), skip),
        let(cp(sel), op(I_SUB, cp(sel), ic(9))),
        skip,
        # every potion in turn, also the ones the hero has none of (the HUD shows it greyed out)
        log('selected ', cp(sel)),
        ret(),
        R_log,
        R,
        t_102,
        let(cp(H), cp(sel)),
    ]
    # the new code goes right before the CanUsePotion check; jumps that went there now enter the new code
    tgt = canuse.old
    for s in st[:i_can]:
        if s.t == 0x07 and s.f[0][1] == tgt:
            s.f[0] = ('jump', new[0])
    st[i_can:i_can] = new
    body, size = f.emit_checked()
    return rebuild(pkg, {f.e['idx']: body}), f'ShHeroController.SelectPotion: next / previous / use selected, {f.script_size} -> {size}'


def repr_names(pkg, n):
    out = []
    def w(x):
        for k, v in x.f:
            if k == 'name': out.append(pkg.names[v])
            if k == 'expr' and v is not None: w(v)
            if k == 'params':
                for y in v: w(y)
    w(n); return ' '.join(out)


if __name__ == '__main__':
    b, msg = patch(Package(sys.argv[1]))
    open(sys.argv[2], 'wb').write(b)
    print(msg)
