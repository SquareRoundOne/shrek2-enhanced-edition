import sys,os; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from pecave import patch_pe, find_cave, call_rel
src,dst=sys.argv[1],sys.argv[2]
cave=find_cave(src,200)
cave2=find_cave(src,400,cave+0x100)
cave3=find_cave(src,64,cave2+0x200)
asm='''
# Top-level GUI components laid out in a centred 4:3 box (V = min(W, H*4/3)),
# except full-width ones (WinWidth >= 0.999) which keep spanning the whole screen.
caveL:
    fld dword ptr [esi+0x78]
    fld dword ptr [kfull]
    fcomip st, st(1)
    fstp st(0)
    jbe fullL
    fild dword ptr [edx+0x90]
    fmul dword ptr [k43]
    fild dword ptr [edx+0x8c]
    fcomi st, st(1)
    fcmovnb st, st(1)
    fstp st(1)
    fild dword ptr [edx+0x8c]
    fsub st, st(1)
    fmul dword ptr [khalf]
    fxch st(1)
    fmul dword ptr [esi+0x74]
    faddp st(1), st
    ret
fullL:
    fild dword ptr [edx+0x8c]
    fmul dword ptr [esi+0x74]
    ret
caveW:
    fld dword ptr [esi+0x78]
    fld dword ptr [kfull]
    fcomip st, st(1)
    fstp st(0)
    jbe fullW
    fild dword ptr [edx+0x90]
    fmul dword ptr [k43]
    fild dword ptr [edx+0x8c]
    fcomi st, st(1)
    fcmovnb st, st(1)
    fstp st(1)
    fmul dword ptr [esi+0x78]
    ret
fullW:
    fild dword ptr [edx+0x8c]
    fmul dword ptr [esi+0x78]
    ret
.balign 4
k43:   .float 1.3333334
khalf: .float 0.5
kfull: .float 0.999
'''
fontasm = r'''
# replaces: Viewport->CreateNativeFont(Face, Size) inside UGUIFont::execGetFont (SAPFont path)
# tries HD bitmap font "SHHDFonts.S<Size>_<Bucket>" first, falls back to the native font.
caveF:
    push ebx
    push esi
    push edi
    push eax                        # face (fallback)
    sub esp, 80                     # wide-char name buffer (40 chars)
    mov edi, esp
    lea esi, [prefix]
copy_prefix:
    movzx eax, word ptr [esi]
    mov [edi], ax
    add esi, 2
    add edi, 2
    test ax, ax
    jnz copy_prefix
    sub edi, 2                      # overwrite terminator
    mov eax, [ebp-0x20]             # size
    call put_int
    mov word ptr [edi], 0x5F        # '_'
    add edi, 2
    mov ecx, [ebp-0x18]             # canvas
    mov ecx, [ecx+0x9c]             # viewport
    mov eax, [ecx+0x90]             # SizeY
    add eax, 180
    xor edx, edx
    mov ecx, 360
    div ecx
    cmp eax, 2
    jge 1f
    mov eax, 2
1:  cmp eax, 6
    jle 2f
    mov eax, 6
2:  imul eax, eax, 360
    call put_int
    mov word ptr [edi], 0
    mov eax, esp                    # name
    push 0                          # Sandbox
    push 2                          # LOAD_NoWarn
    push 0                          # Filename
    push eax                        # Name
    push 0                          # Outer
    mov eax, dword ptr [0x1005e564] # &UFont::PrivateStaticClass
    push eax
    call dword ptr [0x1005e448]     # UObject::StaticLoadObject
    add esp, 24
    add esp, 80
    pop edx                         # face
    test eax, eax
    jnz done
    mov ecx, [ebp-0x18]
    push dword ptr [ebp-0x20]
    mov ecx, [ecx+0x9c]
    mov eax, [ecx]
    push edx
    call dword ptr [eax+0xc8]       # original native font
done:
    pop edi
    pop esi
    pop ebx
    ret

# write eax as decimal wide chars at edi, advance edi
put_int:
    push ebx
    push ecx
    push edx
    mov ebx, 10
    xor ecx, ecx
3:  xor edx, edx
    div ebx
    push edx
    inc ecx
    test eax, eax
    jnz 3b
4:  pop eax
    add eax, 0x30
    mov [edi], ax
    add edi, 2
    loop 4b
    pop edx
    pop ecx
    pop ebx
    ret
.balign 2
prefix: .short 'S','H','H','D','F','o','n','t','s','.','S',0
'''
curasm = '''
# cursor scale: edx = max(1, ClipY/720) << 8   (original: ClipX/640)
caveC:
    push ecx
    mov eax, [esi+0x68]
    xor edx, edx
    mov ecx, 720
    div ecx
    test eax, eax
    jnz 1f
    mov eax, 1
1:  mov edx, eax
    shl edx, 8
    pop ecx
    ret
'''
syms,rel=patch_pe(src,dst,[(cave,asm),(cave2,fontasm),(cave3,curasm)],[
  (0x1001ac9e, bytes.fromhex('db 82 8c 00 00 00 d8 4e 74'), lambda s: call_rel(0x1001ac9e, s['caveL'])),
  (0x1001a9dc, bytes.fromhex('db 82 8c 00 00 00 d8 4e 78'), lambda s: call_rel(0x1001a9dc, s['caveW'])),
  (0x10021d9e, bytes.fromhex('b8 67 66 66 66 f7 eb'), lambda s: call_rel(0x10021d9e, s['caveC'])),
  (0x10019f3c, bytes.fromhex('8b 4d e8 ff 75 e0 8b 89 9c 00 00 00 8b 11 50 ff 92 c8 00 00 00'), lambda s: call_rel(0x10019f3c, s['caveF'])),
], extra_abs=[(0x1005e448,0x1005e44c),(0x1005e564,0x1005e568)])
print('cave',hex(cave),{k:hex(v) for k,v in syms.items() if k.startswith(('cave','k'))},'relocs',[hex(r) for r in rel])
