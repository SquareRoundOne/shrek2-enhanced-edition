"""UnrealScript (UE2, package v129) bytecode disassembler for functions."""
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upkg import Package, Reader

CASTS = {0x39: 'RotToVec', 0x3a: 'ByteToInt', 0x3b: 'ByteToBool', 0x3c: 'ByteToFloat',
         0x3d: 'IntToByte', 0x3e: 'IntToBool', 0x3f: 'IntToFloat', 0x40: 'BoolToByte',
         0x41: 'BoolToInt', 0x42: 'BoolToFloat', 0x43: 'FloatToByte', 0x44: 'FloatToInt',
         0x45: 'FloatToBool', 0x46: 'ObjToIface', 0x47: 'ObjToBool', 0x48: 'NameToBool',
         0x49: 'StrToByte', 0x4a: 'StrToInt', 0x4b: 'StrToBool', 0x4c: 'StrToFloat',
         0x4d: 'StrToVec', 0x4e: 'StrToRot', 0x4f: 'VecToBool', 0x50: 'VecToRot',
         0x51: 'RotToBool', 0x52: 'ByteToStr', 0x53: 'IntToStr', 0x54: 'BoolToStr',
         0x55: 'FloatToStr', 0x56: 'ObjToStr', 0x57: 'NameToStr', 0x58: 'VecToStr',
         0x59: 'RotToStr'}

# Core natives (Object.uc, UE2) - operator numbers
NATIVES = {
    0x81: '!', 0x82: '&&', 0x83: '^^', 0x84: '||', 0x85: '*=b', 0x86: '/=b', 0x87: '+=b',
    0x88: '-=b', 0x89: '++b', 0x8a: '--b', 0x8b: 'b++', 0x8c: 'b--', 0x8d: '~', 0x8e: '-int',
    0x8f: 'int*', 0x90: 'int/', 0x91: 'int+', 0x92: 'int-', 0x93: '<<', 0x94: '>>',
    0x95: 'int<', 0x96: 'int>', 0x97: 'int<=', 0x98: 'int>=', 0x99: 'int==', 0x9a: 'int!=',
    0x9b: 'int&', 0x9c: 'int^', 0x9d: 'int|', 0x9e: 'int*=', 0x9f: 'int/=', 0xa0: 'int+=',
    0xa1: 'int-=', 0xa2: '++int', 0xa3: '--int', 0xa4: 'int++', 0xa5: 'int--', 0xa6: 'Rand',
    0xa7: 'Min', 0xa8: 'Max', 0xa9: '-float', 0xaa: 'float**', 0xab: 'float*', 0xac: 'float/',
    0xad: 'float%', 0xae: 'float+', 0xaf: 'float-', 0xb0: 'float<', 0xb1: 'float>',
    0xb2: 'float<=', 0xb3: 'float>=', 0xb4: 'float==', 0xb5: 'float!=', 0xb6: 'float*=',
    0xb7: 'float/=', 0xb8: 'float+=', 0xb9: 'float-=', 0xba: 'Abs', 0xbb: 'Sin', 0xbc: 'Cos',
    0xbd: 'Tan', 0xbe: 'Atan', 0xbf: 'Exp', 0xc0: 'Loge', 0xc1: 'Sqrt', 0xc2: 'Square',
    0xc3: 'FRand', 0xc4: 'FMin', 0xc5: 'FMax', 0xc6: 'FClamp', 0xc7: 'Lerp', 0xc8: 'Smerp',
    0xd2: '~=', 0xf4: 'FMin?', 0xf5: 'FMax?', 0xf6: 'FClamp?', 0xf9: 'Min?', 0xfa: 'Max?',
    0xfb: 'Clamp', 0x70: '$', 0x71: 'GotoState', 0x72: '==obj', 0x73: '<str', 0x74: '>str',
    0x75: 'Enable', 0x76: 'Disable', 0x77: '!=obj', 0x78: '<=str', 0x79: '>=str',
    0x7a: '==str', 0x7b: '!=str', 0x7c: '~=str', 0x7d: 'Len', 0x7e: 'InStr', 0x7f: 'Mid',
    0x80: 'Left', 0xe7: 'Log', 0xe8: 'Right', 0xe9: 'Caps', 0xea: 'Chr', 0xeb: 'Asc',
    0xfe: '==name', 0xff: '!=name', 0xcb: 'Round?',
}


NATIVES.update({141: '~', 143: '-int', 144: 'int*', 145: 'int/', 146: 'int+', 147: 'int-', 148: '<<', 149: '>>', 150: 'int<', 151: 'int>', 152: 'int<=', 153: 'int>=', 154: 'int==', 155: 'int!=', 156: 'int&', 157: 'int^', 158: 'int|', 159: 'int*=', 160: 'int/=', 161: 'int+=', 162: 'int-=', 163: '++int', 164: '--int', 165: 'int++', 166: 'int--', 167: 'Rand', 244: 'FMin', 245: 'FMax', 246: 'FClamp', 247: 'Lerp', 249: 'Min', 250: 'Max', 251: 'Clamp'})


class Disasm:
    def __init__(self, pkg):
        self.p = pkg
        self.natives = dict(NATIVES)

    def fn_layout(self, e):
        """Return (script_start_offset, script_mem_size, header_info) for a UFunction export."""
        r = Reader(self.p.data, e['offset'])
        if e['flags'] & 0x02000000:  # RF_HasStack
            r.ci(); r.ci(); r.u32(); r.u32()  # node, statenode, probemask(8 bytes in UE2 as qword)
            r.u32(); r.ci()  # latent, offset
        tag = r.ci()
        assert self.p.names[tag] == 'None', self.p.names[tag]
        sup = r.ci(); nxt = r.ci()
        script_text = r.ci(); children = r.ci(); friendly = r.ci(); cpp = r.ci()
        line = r.i32(); textpos = r.i32(); ssize = r.i32()
        return r.p, ssize, dict(children=children, friendly=self.p.names[friendly], line=line)

    def name(self, r):
        return self.p.names[r.ci()]

    def obj(self, r):
        return self.p.fullname(r.ci())

    def expr(self, r, out, depth):
        """Decode one expression; returns text."""
        start = r.p
        t = r.u8()
        def E():
            return self.expr(r, out, depth + 1)
        if t == 0x00: return 'local:' + self.obj(r).split('.')[-1]
        if t == 0x01: return 'self.' + self.obj(r).split('.')[-1]
        if t == 0x02: return 'default.' + self.obj(r).split('.')[-1]
        if t == 0x04: return 'return ' + E()
        if t == 0x05: sz = r.u8(); return f'switch({E()})'
        if t == 0x06: return f'jump ->{r.u16():#06x}'
        if t == 0x07: tgt = r.u16(); return f'if not ({E()}) jump ->{tgt:#06x}'
        if t == 0x08: return 'stop'
        if t == 0x09: r.u16(); return f'assert({E()})'
        if t == 0x0a:
            nxt = r.u16()
            return 'default:' if nxt == 0xffff else f'case {E()}: (next {nxt:#06x})'
        if t == 0x0b: return 'nothing'
        if t == 0x0c:
            labs = []
            while True:
                n = self.name(r); off = r.u32()
                if n == 'None': break
                labs.append(f'{n}@{off:#x}')
            return 'labels ' + ','.join(labs)
        if t == 0x0d: return 'goto ' + E()
        if t == 0x0e: return 'eatstring ' + E()
        if t == 0x0f: a = E(); b = E(); return f'{a} = {b}'
        if t == 0x10: a = E(); b = E(); return f'{b}[{a}]'
        if t == 0x11: return f'new({E()},{E()},{E()},{E()})'
        if t == 0x12:
            a = E(); r.u16(); r.u8(); b = E(); return f'{a}.static.{b}'
        if t == 0x13: c = self.obj(r); return f'class<{c}>({E()})'
        if t == 0x14: a = E(); b = E(); return f'{a} = {b}'
        if t == 0x16: return ')'
        if t == 0x17: return 'self'
        if t == 0x18: r.u16(); return E()
        if t == 0x19:
            a = E(); r.u16(); r.u8(); b = E(); return f'{a}.{b}'
        if t == 0x1a: a = E(); b = E(); return f'{b}[{a}]'
        if t in (0x1b, 0x1c, 0x38):
            if t == 0x1b: fn = self.name(r)
            elif t == 0x1c: fn = self.obj(r).split('.')[-1]
            else: fn = 'global.' + self.name(r)
            return f'{fn}({self.params(r, out, depth)})'
        if t == 0x1d: return str(r.i32())
        if t == 0x1e: return repr(r.f32())
        if t == 0x1f:
            e = self.p.data.index(b'\0', r.p); s = self.p.data[r.p:e].decode('latin-1'); r.p = e + 1
            return repr(s)
        if t == 0x20: return 'obj:' + self.obj(r)
        if t == 0x21: return "'" + self.name(r) + "'"
        if t == 0x22: return f'rot({r.i32()},{r.i32()},{r.i32()})'
        if t == 0x23: return f'vect({r.f32():g},{r.f32():g},{r.f32():g})'
        if t == 0x24: return str(r.u8())
        if t == 0x25: return '0'
        if t == 0x26: return '1'
        if t == 0x27: return 'true'
        if t == 0x28: return 'false'
        if t == 0x29: return 'parm:' + self.obj(r)
        if t == 0x2a: return 'None'
        if t == 0x2c: return str(r.u8())
        if t == 0x2d: return E()
        if t == 0x2e: c = self.obj(r); return f'{c.split(".")[-1]}({E()})'
        if t == 0x2f: a = E(); r.u16(); return f'foreach {a}'
        if t == 0x30: return 'iteratorpop'
        if t == 0x31: return 'iteratornext'
        if t in (0x32, 0x33):
            self.obj(r); a = E(); b = E(); return f'({a} {"==" if t == 0x32 else "!="} {b})'
        if t == 0x34:
            e = r.p
            while self.p.data[e:e + 2] != b'\0\0': e += 2
            s = self.p.data[r.p:e].decode('utf-16le'); r.p = e + 2; return repr(s)
        if t == 0x36:
            m = self.obj(r).split('.')[-1]; return f'{E()}.{m}'
        if t == 0x37: return f'{E()}.Length'
        if t == 0x39:
            c = r.u8(); return f'{CASTS.get(c, hex(c))}({E()})'
        if t == 0x40: a = E(); b = E(); c = E(); return f'{a}.Insert({b},{c})'
        if t == 0x41: a = E(); b = E(); c = E(); return f'{a}.Remove({b},{c})'
        if t == 0x42: r.u32(); r.u32(); r.u32(); r.u8(); return 'debuginfo'
        if t == 0x43: a = self.obj(r).split('.')[-1]; fn = self.name(r); return f'delegate {a}({self.params(r, out, depth)})'
        if t == 0x44: return 'delegateprop:' + self.name(r)
        if t == 0x45: a = E(); b = E(); return f'{a} = {b}'
        if 0x60 <= t <= 0x6f:
            n = ((t - 0x60) << 8) | r.u8()
            return f'native{n}({self.params(r, out, depth)})'
        if t >= 0x70:
            nm = self.natives.get(t, f'native{t:#x}')
            return f'{nm}({self.params(r, out, depth)})'
        raise ValueError(f'unknown token {t:#x} at {start - self.base:#x}')

    def params(self, r, out, depth):
        ps = []
        while True:
            if self.p.data[r.p] == 0x16:
                r.p += 1; break
            ps.append(self.expr(r, out, depth + 1))
        return ', '.join(ps)

    def disasm(self, e):
        start, ssize, info = self.fn_layout(e)
        self.base = start
        r = Reader(self.p.data, start)
        out = []
        end = e['offset'] + e['size']
        # we cannot rely on ssize (memory size); stop when trailer reached
        mempos = 0
        while r.p < end:
            pos = r.p
            try:
                txt = self.expr(r, out, 0)
            except Exception as ex:
                out.append((pos - start, f'!! {ex}'))
                break
            out.append((pos - start, txt))
            if txt == 'return nothing' and end - r.p <= 12:
                break
        return info, out, r.p


def find(pkg, fullname):
    for e in pkg.exports:
        if pkg.fullname(e['idx']) == fullname:
            return e


if __name__ == '__main__':
    pkg = Package(sys.argv[1])
    d = Disasm(pkg)
    for fn in sys.argv[2:]:
        e = find(pkg, fn)
        info, out, endp = d.disasm(e)
        print(f'== {fn}  (line {info["line"]})')
        for off, txt in out:
            print(f'  {off:04x}: {txt}')
