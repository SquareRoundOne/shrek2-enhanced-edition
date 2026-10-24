"""Insert assembled code caves into a PE (x86) and register base relocations.

usage (python): patch_pe(src, dst, caves=[(addr, asm_text)], hooks=[(addr, old_hex, new_bytes_fn)])
Absolute references inside a cave to addresses inside any cave (constants) and the
explicitly listed external absolute addresses (IAT slots) get HIGHLOW relocations.
"""
import os, re, struct, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe import PE


def assemble(addr, src):
    d = tempfile.mkdtemp()
    s = os.path.join(d, 'c.s')
    open(s, 'w').write('.intel_syntax noprefix\n.code32\n.text\n.globl _start\n_start:\n' + src)
    subprocess.check_call(['as', '--32', '-o', s + '.o', s])
    subprocess.check_call(['ld', '-m', 'elf_i386', f'-Ttext={addr:#x}', '-o', s + '.elf', s + '.o'])
    subprocess.check_call(['objcopy', '-O', 'binary', '-j', '.text', s + '.elf', s + '.bin'])
    dis = subprocess.check_output(['objdump', '-d', '-M', 'intel', s + '.elf']).decode()
    syms = {}
    for line in subprocess.check_output(['nm', s + '.elf']).decode().splitlines():
        a, _, n = line.split()
        syms[n] = int(a, 16)
    return open(s + '.bin', 'rb').read(), dis, syms


def abs_fixups(code, addr, dis, ranges):
    """Find imm32/disp32 fields equal to an address inside `ranges` (list of (lo,hi))."""
    fix = []
    for line in dis.splitlines():
        m = re.match(r'^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)', line)
        if not m:
            continue
        ia = int(m.group(1), 16)
        bs = bytes.fromhex(m.group(2))
        for k in range(len(bs) - 3):
            v = struct.unpack_from('<I', bs, k)[0]
            if any(lo <= v < hi for lo, hi in ranges) and not re.search(r'\b(call|jmp|j[a-z]+)\s+0x', line):
                fix.append(ia + k)
    return sorted(set(fix))


def add_relocs(p, addrs):
    f = p.f
    rdir = p.opt + 96 + 5 * 8
    rrva, rsize = struct.unpack_from('<II', f, rdir)
    ri = [i for i, s in enumerate(p.secs) if s[0].rstrip(b'\0') == b'.reloc'][0]
    n, vs, va, rs, ro = p.secs[ri]
    pages = {}
    for a in addrs:
        r = a - p.base
        pages.setdefault(r & ~0xfff, []).append(0x3000 | (r & 0xfff))
    blob = b''
    for page, ents in sorted(pages.items()):
        if len(ents) % 2:
            ents.append(0)
        blob += struct.pack('<II', page, 8 + 2 * len(ents)) + b''.join(struct.pack('<H', e) for e in ents)
    off = ro + rsize
    assert rsize + len(blob) <= rs, 'no room in .reloc'
    assert all(b == 0 for b in f[off:off + len(blob)]), 'reloc slack not empty'
    f[off:off + len(blob)] = blob
    struct.pack_into('<II', f, rdir, rrva, rsize + len(blob))
    if rsize + len(blob) > vs:
        struct.pack_into('<I', f, p.so + ri * 40 + 8, rsize + len(blob))


def patch_pe(src, dst, caves, hooks, extra_abs=()):
    """caves: list of (addr, asm). hooks: list of (addr, expected_old_bytes, replacement_bytes).
    extra_abs: absolute addresses outside caves referenced from caves (e.g. IAT slots)."""
    p = PE(src)
    size0 = len(p.f)
    built = []
    ranges = []
    for addr, asm in caves:
        code, dis, syms = assemble(addr, asm)
        built.append((addr, code, dis, syms))
        ranges.append((addr, addr + len(code)))
    for lo, hi in list(extra_abs):
        ranges.append((lo, hi))
    relocs = []
    allsyms = {}
    for addr, code, dis, syms in built:
        o = p.v2o(addr)
        assert all(b == 0xCC for b in p.f[o:o + len(code)]), f'cave {addr:#x} not free'
        p.f[o:o + len(code)] = code
        relocs += abs_fixups(code, addr, dis, ranges)
        allsyms.update(syms)
    for addr, old, new in hooks:
        new = new(allsyms) if callable(new) else new
        o = p.v2o(addr)
        assert bytes(p.f[o:o + len(old)]) == old, (hex(addr), p.f[o:o + len(old)].hex())
        assert len(new) <= len(old)
        p.f[o:o + len(old)] = new + b'\x90' * (len(old) - len(new))
    if relocs:
        add_relocs(p, relocs)
    assert len(p.f) == size0, 'file size changed'
    open(dst, 'wb').write(p.f)
    return allsyms, relocs


def call_rel(frm, to):
    return b'\xe8' + struct.pack('<i', to - (frm + 5))


def jmp_rel(frm, to):
    return b'\xe9' + struct.pack('<i', to - (frm + 5))


def find_cave(path, size, start=0):
    p = PE(path)
    n, vs, va, rs, ro = p.secs[0]
    data = bytes(p.f[ro:ro + vs])
    for m in re.finditer(rb'\xcc{%d,}' % (size + 16), data):
        a = p.base + va + m.start()
        a = (a + 15) & ~15
        if a >= start:
            return a
