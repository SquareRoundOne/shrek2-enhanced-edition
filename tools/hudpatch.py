"""Widescreen HUD + resolution-independent fonts patch for Shrek 2 (PC, 2005 RU build).

Rewrites UnrealScript bytecode of HUD functions in KWGame.u / SHGame.u:
  * every  frac * SizeX  used for HUD geometry  ->  frac * V,  V = SizeY * 4/3
    (the HUD keeps its 4:3 proportions at any aspect ratio);
  * X positions get an anchor offset  a * (SizeX - V)  (a = 0 left, 0.5 centre, 1 right);
  * HUD icon widths use the vertical scale (fScaleY) instead of the horizontal one;
  * native HUD fonts (SAPFont) are created with sizes scaled by SizeY/768.
"""
import copy, struct, sys, os, hashlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upkg import Package
from ubc import Func, Node, walk
from upkgw import rebuild

F_MUL, F_DIV, F_ADD, F_SUB = 0xAB, 0xAC, 0xAE, 0xAF
CAST, CTX, LOCAL, INST, FCONST = 0x39, 0x19, 0x00, 0x01, 0x1E
INT2FLOAT, FLOAT2INT = 0x3F, 0x44


class Ctx:
    def __init__(self, pkg):
        self.pkg = pkg
        self.nidx = {n: i for i, n in enumerate(pkg.names)}

    def ref(self, suffix):
        p = self.pkg
        for e in p.exports:
            if p.fullname(e['idx']).endswith(suffix):
                return e['idx']
        for i in range(len(p.imports)):
            if p.fullname(-(i + 1)).endswith(suffix):
                return -(i + 1)
        raise KeyError(suffix)

    def refname(self, r):
        return self.pkg.objname(r)

    def name(self, n):
        return self.nidx[n]


# ---------- node builders
def fconst(v):
    return Node(FCONST, [('f32', struct.pack('<f', v))])

def nat(op, *args):
    return Node(op, [('params', list(args))])

def cast(c, e):
    return Node(CAST, [('u8', c), ('expr', e)])

def ctx(obj, member, size=4):
    return Node(CTX, [('expr', obj), ('skip', None), ('u8', size), ('expr', member)])

def inst(ref):
    return Node(INST, [('ref', ref)])


# ---------- pattern helpers
def is_tok(n, t):
    return isinstance(n, Node) and n.t == t

def ctx_member_ref(n):
    """For Context(obj, Instance X) return (obj_node, X_ref)."""
    if is_tok(n, CTX):
        obj = n.f[0][1]; mem = n.f[3][1]
        if is_tok(mem, INST):
            return obj, mem.f[0][1]
    return None, None

def is_int2float_sizex(c, n, sizex):
    """IntToFloat(<canvas>.SizeX) -> returns canvas node or None"""
    if is_tok(n, CAST) and n.f[0][1] == INT2FLOAT:
        obj, r = ctx_member_ref(n.f[1][1])
        if r == sizex:
            return obj
    return None

def contains(n, pred):
    if pred(n):
        return True
    return any(contains(x, pred) for x in n.exprs())


class FuncPatcher:
    def __init__(self, c, func, canvas=None):
        self.c = c
        self.f = func
        self.sizex = c.ref('Canvas.SizeX')
        self.sizey = c.ref('Canvas.SizeY')
        self.changed = 0
        self.canvas = canvas

    def V(self, canvas):
        """float*(IntToFloat(canvas.SizeY), 4/3)"""
        return nat(F_MUL, cast(INT2FLOAT, ctx(copy.deepcopy(canvas), inst(self.sizey))), fconst(4.0 / 3.0))

    def E(self, canvas, a):
        """a * (SizeX - V)"""
        e = nat(F_SUB, cast(INT2FLOAT, ctx(copy.deepcopy(canvas), inst(self.sizex))), self.V(canvas))
        if a == 1:
            return e
        return nat(F_MUL, e, a if isinstance(a, Node) else fconst(a))

    # ---- transforms
    def widths_to_v(self):
        """float*(A, IntToFloat(C.SizeX)) -> float*(A, V)"""
        def fn(n):
            if n.t == F_MUL:
                ps = n.f[0][1]
                if len(ps) == 2:
                    cv = is_int2float_sizex(self.c, ps[1], self.sizex)
                    if cv is not None:
                        self.canvas = self.canvas or cv
                        ps[1] = self.V(cv); self.changed += 1
            return n
        for s in self.f.stmts:
            walk(s, fn)

    def find_canvas(self):
        if self.canvas is not None:
            return self.canvas
        found = []
        def fn(n):
            obj, r = ctx_member_ref(n)
            if r in (self.sizex, self.sizey) and obj is not None and is_tok(obj, LOCAL):
                found.append(obj)
            return n
        for s in self.f.stmts:
            walk(s, fn)
        if not found:
            # fall back to the function's own parameter named Canvas / C
            p = self.c.pkg
            for e in p.exports:
                if e['package'] == self.f.e['idx'] and e['name'] in ('Canvas', 'C'):
                    found.append(Node(LOCAL, [('ref', e['idx'])]))
        self.canvas = found[0]
        return self.canvas

    def _is_v(self, n):
        return n.t == F_MUL and len(n.f[0][1]) == 2 and getattr(n.f[0][1][1], 't', None) == FCONST \
            and n.f[0][1][1].f[0][1] == struct.pack('<f', 4.0 / 3.0)

    def setpos_anchor(self, a):
        """SetPos(x, y): x -> x + a*(SizeX-V) when x was derived from the 4:3 width."""
        sp = self.c.ref('Canvas.SetPos')
        def fn(n):
            if n.t == 0x1C and n.f[0][1] == sp:
                ps = n.f[1][1]
                if contains(ps[0], self._is_v):
                    ps[0] = nat(F_ADD, ps[0], self.E(self.find_canvas(), a)); self.changed += 1
            return n
        for s in self.f.stmts:
            walk(s, fn)

    def return_x_anchor(self, a):
        """return FloatToInt(X) -> return FloatToInt(X + a*(SizeX-V))"""
        for s in self.f.stmts:
            if s.t == 0x04 and is_tok(s.f[0][1], CAST) and s.f[0][1].f[0][1] == FLOAT2INT:
                inner = s.f[0][1]
                inner.f[1] = ('expr', nat(F_ADD, inner.f[1][1], self.E(self.find_canvas(), a)))
                self.changed += 1
                return

    def swap_names(self, mapping):
        """VirtualFunction name swaps, e.g. GetScaleX -> GetScaleY"""
        m = {self.c.name(a): self.c.name(b) for a, b in mapping.items()}
        def fn(n):
            if n.t in (0x1B, 0x38) and n.f[0][1] in m:
                n.f[0] = ('name', m[n.f[0][1]]); self.changed += 1
            return n
        for s in self.f.stmts:
            walk(s, fn)

    def swap_refs(self, mapping):
        m = {self.c.ref(a): self.c.ref(b) for a, b in mapping.items()}
        def fn(n):
            for i, (k, v) in enumerate(n.f):
                if k == 'ref' and v in m:
                    n.f[i] = ('ref', m[v]); self.changed += 1
            return n
        for s in self.f.stmts:
            walk(s, fn)

    def scale_fonts(self, base_h):
        """CreateNativeFont(Name, Size) -> CreateNativeFont(Name, FloatToInt(Size*SizeY/base_h))"""
        cn = self.c.ref('Canvas.CreateNativeFont')
        canvas = self.find_canvas_any()
        def fn(n):
            if n.t == 0x1C and n.f[0][1] == cn:
                ps = n.f[1][1]
                sy = cast(INT2FLOAT, ctx(copy.deepcopy(canvas), inst(self.sizey)))
                ps[1] = cast(FLOAT2INT, nat(F_MUL, cast(INT2FLOAT, ps[1]), nat(F_DIV, sy, fconst(float(base_h)))))
                self.changed += 1
            return n
        for s in self.f.stmts:
            walk(s, fn)

    def load_fov(self):
        """After loading a save: take the player's FOV from User.ini (Engine.PlayerController DefaultFOV,
           written by the FOV console command) instead of the value stored in the save:
             if (StrToFloat(GetIniEntry("Engine.PlayerController","DefaultFOV","user")) > 1.0) {
                 DefaultFOV = StrToFloat(...); }
             FovAngle = DefaultFOV; DesiredFOV = DefaultFOV;"""
        c = self.c; pkg = c.pkg
        stm = lambda name: Func(pkg, get_func(pkg, name)).stmts
        refs = {}
        def grab(n):
            if n.t == 0x01: refs.setdefault(pkg.objname(n.f[0][1]), n.f[0][1])
            return n
        for st in stm('FOVController.Init') + stm('FOVController.Finish'):
            walk(st, grab)
        S = lambda t: Node(0x1F, [('zstr', t.encode() + b'\0')])
        ini = lambda: cast(0x4C, Node(0x1B, [('name', c.name('GetIniEntry')),
                                              ('params', [S('Engine.PlayerController'), S('DefaultFOV'), S('user')])]))
        last = self.f.stmts[-1]
        assert last.t == 0x04
        set_fovs = [Node(0x0F, [('expr', inst(refs['FovAngle'])), ('expr', inst(refs['DefaultFOV']))]),
                    Node(0x0F, [('expr', inst(refs['DesiredFOV'])), ('expr', inst(refs['DefaultFOV']))])]
        jif = Node(0x07, [('jump', set_fovs[0]), ('expr', nat(0xB1, ini(), fconst(1.0)))])
        assign = Node(0x0F, [('expr', inst(refs['DefaultFOV'])), ('expr', ini())])
        self.f.stmts[-1:-1] = [jif, assign] + set_fovs
        self.changed += 4

    def debug_fov(self):
        """Diagnostic overlay: 'FOV <FovAngle>/<DesiredFOV>/<DefaultFOV>  <SizeX>x<SizeY>'"""
        c = self.c; pkg = c.pkg
        stm = lambda name: Func(pkg, get_func(pkg, name)).stmts
        pc = stm('FOVController.BeginPlay')[1].f[1][1]
        refs = {}
        def grab(n):
            if n.t == 0x01: refs.setdefault(pkg.objname(n.f[0][1]), n.f[0][1])
            return n
        for st in stm('FOVController.Init') + stm('FOVController.Finish'):
            walk(st, grab)
        cv = None
        for e in pkg.exports:
            if e['package'] == self.f.e['idx'] and e['name'] in ('C', 'Canvas'):
                cv = Node(LOCAL, [('ref', e['idx'])])
        S = lambda t: Node(0x1F, [('zstr', t.encode() + b'\0')])
        cat = lambda a, b: nat(0x70, a, b)
        f2s = lambda e: cast(0x55, e)
        i2s = lambda e: cast(0x53, e)
        pcm = lambda n: ctx(copy.deepcopy(pc), inst(refs[n]))
        cvm = lambda r: ctx(copy.deepcopy(cv), inst(r))
        txt = cat(cat(cat(cat(cat(cat(S('FOV '), f2s(pcm('FovAngle'))), S(' / ')), f2s(pcm('DesiredFOV'))), S(' / ')),
                      f2s(pcm('DefaultFOV'))),
                  cat(S('   '), cat(i2s(cvm(self.sizex)), cat(S('x'), i2s(cvm(self.sizey))))))
        setpos = ctx(copy.deepcopy(cv), Node(0x1C, [('ref', c.ref('Canvas.SetPos')), ('params', [fconst(20.0),
                     nat(F_MUL, cast(INT2FLOAT, cvm(self.sizey)), fconst(0.25))])]), 0)
        draw = ctx(copy.deepcopy(cv), Node(0x61, [('u8', 0xD1), ('params', [txt])]), 0)
        fcls = c.ref('Engine.Font')
        font = Node(0x2E, [('ref', fcls), ('expr', Node(0x1C, [('ref', c.ref('Object.DynamicLoadObject')),
                    ('params', [S('SHHDFonts.S14_1080'), Node(0x20, [('ref', fcls)])])]))])
        setfont = Node(0x0F, [('expr', cvm(c.ref('Canvas.Font'))), ('expr', font)])
        self.f.stmts[0:0] = [setfont, setpos, draw]
        self.changed += 2

    def reset_fov(self):
        """Append before the final return:
             PC = KWGame(Level.Game).GetHeroController();  PC.FovAngle = PC.DefaultFOV;  PC.DesiredFOV = PC.DefaultFOV
           (skipping a cutscene could leave the cutscene camera FOV active in gameplay)"""
        c = self.c; pkg = c.pkg
        def fn_stmts(name):
            return Func(pkg, get_func(pkg, name)).stmts
        pc = fn_stmts('FOVController.BeginPlay')[1].f[1][1]          # KWGame(Level.Game).GetHeroController()
        assert pc.t == 0x19, pc
        pcref = {}
        def grab(n):
            if n.t == 0x01:
                pcref.setdefault(pkg.objname(n.f[0][1]), n.f[0][1])
            return n
        for st in fn_stmts('FOVController.Init') + fn_stmts('FOVController.Finish'):
            walk(st, grab)
        def member(name):
            return ctx(copy.deepcopy(pc), inst(pcref[name]))
        new = [Node(0x0F, [('expr', member('FovAngle')), ('expr', member('DefaultFOV'))]),
               Node(0x0F, [('expr', member('DesiredFOV')), ('expr', member('DefaultFOV'))])]
        last = self.f.stmts[-1]
        assert last.t == 0x04
        self.f.stmts[-1:-1] = new
        self.changed += 2

    def hd_fonts(self, pkgname):
        """X = Canvas.CreateNativeFont(Name, Size)  ->
           X = Font(DynamicLoadObject("<pkg>.<Style>" $ string(Clamp((SizeY+180)/360,2,6)*360), class'Font'))"""
        c = self.c
        cn = c.ref('Canvas.CreateNativeFont')
        dlo = c.ref('Object.DynamicLoadObject')
        fcls = c.ref('Engine.Font')
        canvas = self.find_canvas_any()
        def ic(v):
            return Node(0x1D, [('i32', v)])
        for s in self.f.stmts:
            if s.t != 0x0F:
                continue
            lhs, rhs = s.f[0][1], s.f[1][1]
            if not (is_tok(rhs, CTX) and is_tok(rhs.f[3][1], 0x1C) and rhs.f[3][1].f[0][1] == cn):
                continue
            var = c.pkg.objname(lhs.f[0][1])          # e.g. SHBigFont
            style = {'Big': 'S24_', 'Med': 'S18_', 'Small': 'S14_', 'Tiny': 'S12_'}[next(st for st in ('Big', 'Med', 'Small', 'Tiny') if st in var)]
            sizey = ctx(copy.deepcopy(canvas), inst(self.sizey))
            bucket = nat(0x90, Node(0xFB, [('params', [nat(0x91, nat(0x92, sizey, ic(180)), ic(360)), ic(2), ic(6)])]), ic(360))
            name = nat(0x70, Node(0x1F, [('zstr', f'{pkgname}.{style}'.encode('latin-1') + b'\0')]), cast(0x53, bucket))
            call = Node(0x1C, [('ref', dlo), ('params', [name, Node(0x20, [('ref', fcls)])])])
            s.f[1] = ('expr', Node(0x2E, [('ref', fcls), ('expr', call)]))
            self.changed += 1

    def text_y_divisor(self, div):
        """fTextY = ... - fYTextLen/2  ->  ... - fYTextLen/div  (Tahoma digits sit high in the cell)"""
        ly = None
        for e in self.c.pkg.exports:
            if e['package'] == self.f.e['idx'] and e['name'] == 'fYTextLen':
                ly = e['idx']
        for s in self.f.stmts:
            if s.t == 0x0F and is_tok(s.f[0][1], LOCAL) and self.c.pkg.objname(s.f[0][1].f[0][1]) == 'fTextY':
                def fn(n):
                    if n.t == F_DIV:
                        a, b = n.f[0][1]
                        if is_tok(a, LOCAL) and a.f[0][1] == ly:
                            n.f[0] = ('params', [a, fconst(div)]); self.changed += 1
                    return n
                walk(s, fn)

    def find_canvas_any(self):
        if self.canvas is not None:
            return self.canvas
        found = []
        cn = self.c.ref('Canvas.CreateNativeFont')
        def fn(n):
            if is_tok(n, CTX) and is_tok(n.f[3][1], 0x1C) and n.f[3][1].f[0][1] == cn:
                found.append(n.f[0][1])
            return n
        for s in self.f.stmts:
            walk(s, fn)
        self.canvas = found[0]
        return self.canvas


def get_func(pkg, fullname):
    for e in pkg.exports:
        if pkg.classname(e) == 'Function' and pkg.fullname(e['idx']) == fullname:
            return e
    raise KeyError(fullname)


# Recipes: (function, [operations])
CENTER, RIGHT, LEFT = 0.5, 1, 0
KW_RECIPES = [
    ('KWHud.RenderToScale', [('refs', {'KWHud.fScaleX': 'KWHud.fScaleY'})]),
    ('KWHudItem.DrawCount', [('names', {'GetScaleX': 'GetScaleY'})]),
    ('KWHudItem.GetItemXPos', [('v',), ('retx', 'zone')]),
    ('KWCutTextController.Hourglass.RenderHud', [('names', {'GetScaleX': 'GetScaleY'})]),
    ('KWHudItemTimer.RenderHud', [('v',), ('setpos', CENTER)]),
    ('KWHudItemHealthBoss.RenderHud', [('v',), ('setpos', CENTER)]),
    ('KWHud.SetupLanguage', [('fonts', 768)]),
    ('KWHud.EndCutScene', [('resetfov',)]),
    ('KWHud.BypassCutscene', [('resetfov',)]),
    ('KWHeroController.PostLoadGame', [('loadfov',)]),
] + ([('KWHud.PostRender', [('debugfov',)])] if os.environ.get('FOVDEBUG') else []) + [
]
SH_RECIPES = [
    ('SHHudItem.DrawCount', [('refs', {'KWHud.fScaleX': 'KWHud.fScaleY'}), ('texty', 1.5)]),
    ('HudItemCoins.GetItemXPos', [('names', {'GetScaleXNoCanvas': 'GetScaleYNoCanvas'}), ('retx', RIGHT)]),
    ('HudItemPotion.GetItemXPos', [('names', {'GetScaleXNoCanvas': 'GetScaleYNoCanvas'}), ('retx', RIGHT)]),
    ('HudItemTimesUp.GetItemXPos', [('names', {'GetScaleXNoCanvas': 'GetScaleYNoCanvas'}), ('retx', CENTER)]),
    ('HudItemTimesUp.DrawHudItem', [('v',), ('setpos', CENTER)]),
    ('HudItemHealthBar.UpdateIconSizing', [('v',)]),
    ('HudItemHealthBar.UpdateGlowSizing', [('v',)]),
    ('HudItemHealthBoss.RenderHud', [('v',), ('setpos', CENTER)]),
    ('HudItemHealthBossPib.RenderGenericHud', [('v',), ('setpos', CENTER)]),
    ('HudItemHealthBossPib.RenderFGMHud', [('v',), ('setpos', CENTER)]),
    ('HudItemHealthBossPib.RenderFatKnightHud', [('v',), ('setpos', CENTER)]),
    ('HudItemHealthBossPib.RenderPIBHud', [('v',), ('setpos', CENTER)]),
    ('HudItemHealthBossPib.RenderBanditHud', [('v',), ('setpos', CENTER)]),
    ('SHHudGameTimer.RenderHud', [('v',), ('setpos', RIGHT)]),
    ('SHHudPotionTimer.RenderHud', [('v',), ('setpos', RIGHT)]),
    ('SHCutTextController.Hourglass.RenderHud', [('names', {'GetScaleX': 'GetScaleY'})]),
    ('SHHud.DrawLevelAction', [('v',), ('setpos', CENTER)]),
    ('SHHud.SetupLanguage', [('hdfonts', 'SHHDFonts')]),
    ('SHPotionSelectMenu.Internal_OnDraw', [('v',), ('setpos', CENTER)]),
]


def apply(pkg, recipes, log):
    c = Ctx(pkg)
    bodies = {}
    for fname, ops in recipes:
        e = get_func(pkg, fname)
        f = Func(pkg, e)
        fp = FuncPatcher(c, f)
        for op in ops:
            if op[0] == 'v':
                fp.widths_to_v()
            elif op[0] == 'setpos':
                fp.setpos_anchor(op[1])
            elif op[0] == 'retx':
                a = op[1]
                if a == 'zone':
                    # a = FClamp(3*TextureLeft - 1, 0, 1)
                    tl = inst(c.ref('KWHudItem.TextureLeft'))
                    a = Node(0xF6, [('params', [nat(F_SUB, nat(F_MUL, fconst(3.0), tl), fconst(1.0)),
                                                fconst(0.0), fconst(1.0)])])
                fp.return_x_anchor(a)
            elif op[0] == 'names':
                fp.swap_names(op[1])
            elif op[0] == 'refs':
                fp.swap_refs(op[1])
            elif op[0] == 'fonts':
                fp.scale_fonts(op[1])
            elif op[0] == 'loadfov':
                fp.load_fov()
            elif op[0] == 'debugfov':
                fp.debug_fov()
            elif op[0] == 'resetfov':
                fp.reset_fov()
            elif op[0] == 'hdfonts':
                fp.hd_fonts(op[1])
            elif op[0] == 'texty':
                fp.text_y_divisor(op[1])
        body, size = f.emit_checked()
        log.append(f'  {fname}: {fp.changed} change(s), script {f.script_size} -> {size} bytes')
        if fp.changed == 0:
            raise RuntimeError(f'no change applied in {fname}')
        bodies[e['idx']] = body
    return rebuild(pkg, bodies)


if __name__ == '__main__':
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    log = []
    for name, rec in (('KWGame.u', KW_RECIPES), ('SHGame.u', SH_RECIPES)):
        pkg = Package(os.path.join(src, name))
        log.append(name)
        out = apply(pkg, rec, log)
        open(os.path.join(dst, name), 'wb').write(out)
    print('\n'.join(log))
