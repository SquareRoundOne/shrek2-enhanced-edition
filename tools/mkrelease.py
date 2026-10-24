r"""Shrek2_EnhancedEdition_v<ver>.zip from rel0933 (scripts, README), the payload of the installed package
(<game>\System\ShrekRemaster\files) with the fresh builds laid over it, and the launcher.
usage: mkrelease.py <game System folder> <out zip>
The installed package's files hold the patched Engine.dll, Game.exe etc.; dinput8.dll, shrz.dll and the launcher come from out/,
SHGame.u and KWGame.u (patched by tools/*patch.py) from the game folder. SHHDFonts.utx is left out: shrz.dll builds it at first run."""
import os, sys, zipfile

G = sys.argv[1].rstrip('/\\') + os.sep
OUT = sys.argv[2]
HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
R = os.path.join(PROJ, 'rel0933') + os.sep
O = os.path.join(PROJ, 'out') + os.sep

entries = {}
for root, _, fs in os.walk(R):
    for f in fs:
        p = os.path.join(root, f)
        entries[os.path.relpath(p, R).replace(os.sep, '/')] = p
base = G + 'ShrekRemaster' + os.sep + 'files' + os.sep
for root, _, fs in os.walk(base):
    for f in fs:
        p = os.path.join(root, f)
        rel = os.path.relpath(p, base).replace(os.sep, '/')
        if rel == 'SHHDFonts.utx':
            continue
        entries.setdefault('System/ShrekRemaster/files/' + rel, p)   # the project's own small files (ShrekPad.ini, scripts) win over the installed copies
orig = G + 'ShrekRemaster' + os.sep + 'original' + os.sep      # the untouched Russian originals: install.ps1 -Uninstall needs them
for f in os.listdir(orig):
    entries.setdefault('System/ShrekRemaster/original/' + f, orig + f)
for k, v in {'dinput8.dll': O + 'dinput8.dll', 'shrz.dll': O + 'shrz.dll', 'SHGame.u': G + 'SHGame.u', 'KWGame.u': G + 'KWGame.u'}.items():
    entries['System/ShrekRemaster/files/' + k] = v
entries['Shrek 2 Enhanced Edition.exe'] = O + 'Shrek2EE.exe'   # next to the System folder (install.ps1 looks for it there)
with zipfile.ZipFile(OUT, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
    for k in sorted(entries):
        z.write(entries[k], k)
print(len(entries), 'files', os.path.getsize(OUT) // 1024, 'KB')
for k in sorted(entries):
    if '/files/' in k or k.endswith('.exe'):
        print(' ', k, os.path.getsize(entries[k]))
