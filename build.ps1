# Сборка shrz.dll, dinput8.dll и лаунчера (Windows, PowerShell).
# Нужен llvm-mingw (ucrt, x86_64): https://github.com/mstorsjo/llvm-mingw/releases  (llvm-mingw-20240619-ucrt-x86_64.zip)
# Использование:  .\build.ps1 -Tool C:\tools\llvm-mingw  [-Only shrz|pad|launcher]
param([string]$Tool = "C:\tools\llvm-mingw", [string]$Only = "")
$ErrorActionPreference = "Stop"
$env:PATH = "$Tool\bin;$env:PATH"
$root = $PSScriptRoot
$out = Join-Path $root "out"; New-Item -ItemType Directory -Force $out | Out-Null
$stb = Join-Path $root "ext\stb"
if (-not $Only -or $Only -eq "shrz") {
  Push-Location "$root\rz"
  i686-w64-mingw32-clang++ -O2 -std=c++17 -shared -static "-I$stb" -w shrz.cpp pp9.cpp rumble.cpp prof.cpp subs.cpp shrz.def -o "$out\shrz.dll" -lwinmm -luuid
  Pop-Location; Write-Host "shrz.dll ok"
}
if (-not $Only -or $Only -eq "pad") {
  Push-Location "$root\pad"
  i686-w64-mingw32-clang++ -O2 -std=c++17 -shared -static -w dinput8.cpp ..\launcher\rgbcx.cpp dinput8.def -o "$out\dinput8.dll" -lhid -lsetupapi -lws2_32 -lgdi32 -lshell32 -luuid -ldxguid
  Pop-Location; Write-Host "dinput8.dll ok"
}
if (-not $Only -or $Only -eq "launcher") {
  Push-Location "$root\launcher"
  x86_64-w64-mingw32-clang++ -O2 -std=c++20 -static -mwindows -municode -w win_main.cpp libs.cpp rgbcx.cpp -o "$out\Shrek2EE.exe" -ldwmapi -lshell32 -lgdi32 -lwinmm
  Pop-Location; Write-Host "Shrek2EE.exe ok"
}
Write-Host "Готово: $out"
