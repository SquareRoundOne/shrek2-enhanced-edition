"""D3DDrv.dll: optional MSAA for the main swap chain.
The sample count lives in the DLL after the marker 'SHRMSAA:' (installer rewrites it; 0/1 = off)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pecave import patch_pe, call_rel, find_cave

src, dst, level = sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 4
cave = find_cave(src, 64)
HOOK = 0x1001673b   # mov dword [ecx+0x46f8],1   (PresentParams.Flags = LOCKABLE_BACKBUFFER)
asm = f'''
    mov dword ptr [ecx+0x46f8], 1
    mov eax, dword ptr [msaa_level]
    cmp eax, 2
    jb 1f
    mov dword ptr [ecx+0x46e0], eax      /* MultiSampleType */
    mov dword ptr [ecx+0x46f8], 0        /* MSAA back buffers are not lockable */
1:  ret
    .ascii "SHRMSAA:"
msaa_level: .long {level}
'''
syms, rel = patch_pe(src, dst, [(cave, asm)],
                     [(HOOK, bytes.fromhex('c781f846000001000000'), lambda s: call_rel(HOOK, cave))])
print('cave', hex(cave), 'relocs', [hex(r) for r in rel])
