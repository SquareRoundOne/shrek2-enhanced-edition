param([switch]$Uninstall)
$ErrorActionPreference = 'Stop'

$here   = Split-Path -Parent $MyInvocation.MyCommand.Path      # ...\System\ShrekRemasterVideos
$system = Split-Path -Parent $here
$root   = Split-Path -Parent $system
$movies = Join-Path $root 'Movies'
$files  = Join-Path $here 'files'
$backup = Join-Path $here 'backup'

if (-not (Test-Path (Join-Path $system 'Game.exe'))) { throw "Game.exe не найден в $system. Распакуйте архив в папку игры." }
if (-not (Test-Path $movies)) { throw "Папка Movies не найдена: $movies" }

$names = Get-ChildItem $files -Filter *.bik | ForEach-Object { $_.Name }

if ($Uninstall) {
    foreach ($n in $names) {
        $b = Join-Path $backup $n
        if (Test-Path $b) { Copy-Item $b (Join-Path $movies $n) -Force; Write-Host "Восстановлен оригинал Movies\$n" }
    }
    Write-Host 'Готово: оригинальные ролики возвращены.'
    return
}

# physical resolution of the primary display
Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices;
public static class DispV {
  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
  public struct DEVMODE {
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)] public string dmDeviceName;
    public short dmSpecVersion, dmDriverVersion, dmSize, dmDriverExtra; public int dmFields;
    public int dmPositionX, dmPositionY, dmDisplayOrientation, dmDisplayFixedOutput;
    public short dmColor, dmDuplex, dmYResolution, dmTTOption, dmCollate;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)] public string dmFormName;
    public short dmLogPixels; public int dmBitsPerPel, dmPelsWidth, dmPelsHeight, dmDisplayFlags, dmDisplayFrequency;
    public int dmICMMethod, dmICMIntent, dmMediaType, dmDitherType, dmReserved1, dmReserved2, dmPanningWidth, dmPanningHeight;
  }
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool EnumDisplaySettings(string dev, int mode, ref DEVMODE dm);
  public static int[] Size() { var d = new DEVMODE(); d.dmSize = (short)Marshal.SizeOf(d); EnumDisplaySettings(null, -1, ref d); return new int[]{ d.dmPelsWidth, d.dmPelsHeight }; }
}
'@
$sz = [DispV]::Size()
Write-Host "Разрешение экрана: $($sz[0])x$($sz[1])"

if ($sz[1] -lt 1080 -or $sz[0] -lt 1440) {
    Write-Host 'Экран меньше 1440x1080: HD-ролики не поместятся, оставляю оригинальные.'
    return
}
# The game copies movie frames 1:1. Bink's own "double width and height" header flag
# (0x40000000 at offset 36) makes binkw32 present 1440x1080 as 2880x2160 when the screen allows.
$double = ($sz[0] -ge 2880 -and $sz[1] -ge 2160)

New-Item -ItemType Directory -Force $backup | Out-Null
foreach ($n in $names) {
    $dst = Join-Path $movies $n
    $b   = Join-Path $backup $n
    if (-not (Test-Path $b) -and (Test-Path $dst)) { Copy-Item $dst $b }
    $bytes = [IO.File]::ReadAllBytes((Join-Path $files $n))
    $flags = [BitConverter]::ToUInt32($bytes, 36) -band 0x8FFFFFFF
    if ($double) { $flags = $flags -bor 0x40000000 }
    [BitConverter]::GetBytes([uint32]$flags).CopyTo($bytes, 36)
    [IO.File]::WriteAllBytes($dst, $bytes)
    Write-Host ("Установлен Movies\{0} ({1})" -f $n, $(if ($double) { '2880x2160' } else { '1440x1080' }))
}
Write-Host ''
Write-Host 'Готово.'
