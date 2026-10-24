param([switch]$Uninstall, [ValidateSet(0,2,4,8)][int]$Msaa = 4)
$ErrorActionPreference = 'Stop'

$here   = Split-Path -Parent $MyInvocation.MyCommand.Path      # ...\System\ShrekRemaster
$system = Split-Path -Parent $here                              # ...\System
$root   = Split-Path -Parent $system                            # game folder
$files  = Join-Path $here 'files'
$backup = Join-Path $here 'backup'
$orig   = Join-Path $here 'original'      # untouched game files shipped for uninstall
$exe    = Join-Path $system 'Game.exe'
$layers = 'HKCU:\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers'

# file in the package -> destination relative to the game folder
$map = [ordered]@{
    'Game.exe'      = 'System\Game.exe'
    'Core.dll'      = 'System\Core.dll'
    'D3DDrv.dll'    = 'System\D3DDrv.dll'
    'Engine.dll'    = 'System\Engine.dll'
    'Engine.u'      = 'System\Engine.u'
    'GUI.dll'       = 'System\GUI.dll'
    'GUI.u'         = 'System\GUI.u'
    'WinDrv.dll'    = 'System\WinDrv.dll'
    'KWGame.u'      = 'System\KWGame.u'
    'SHGame.u'      = 'System\SHGame.u'
    'SAPFont.rus'   = 'System\SAPFont.rus'
    'SAPFont.int'   = 'System\SAPFont.int'
    'GameMenu.int'  = 'System\Cutscenes\GameMenu.int'
    'dinput8.dll'   = 'System\dinput8.dll'
    'shrz.dll'      = 'System\shrz.dll'
    'OpenAL32.dll'  = 'System\OpenAL32.dll'
    'LevelSelect.bat' = 'System\LevelSelect.bat'
    'BumpSet.int'   = 'System\BumpSet.int'
}
# the game keeps the player's User.ini in Documents\Shrek 2 (Core: SHGetFolderPath(CSIDL_PERSONAL) + AppUserDir)
$docUser   = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Shrek 2\User.ini'
$docBackup = Join-Path $backup 'Documents\User.ini'
$iniFiles = @('System\Default.ini', 'System\DefUser.ini', 'System\User.ini', 'System\Shrek2.ini', 'System\dxvk.conf')

if (-not (Test-Path $exe)) { throw "Game.exe не найден в $system. Распакуйте архив в папку игры." }
# the game keeps User.ini in memory and writes it back on exit: installing while it runs loses the changes
$running = Get-Process -Name Game -ErrorAction SilentlyContinue | Where-Object { $_.Path -and ($_.Path -ieq $exe) }
if ($running) { throw 'Игра запущена. Закройте её и запустите установку ещё раз.' }

function Set-IniValues([string]$path, [string]$section, [hashtable]$values) {
    $enc   = [System.Text.Encoding]::Default
    $lines = [System.Collections.Generic.List[string]]([IO.File]::ReadAllLines($path, $enc))
    $start = -1
    for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i].Trim() -ieq "[$section]") { $start = $i; break } }
    if ($start -lt 0) { return }
    $end = $lines.Count
    for ($i = $start + 1; $i -lt $lines.Count; $i++) { if ($lines[$i].TrimStart().StartsWith('[')) { $end = $i; break } }
    foreach ($k in $values.Keys) {
        $found = $false
        for ($i = $start + 1; $i -lt $end; $i++) {
            if ($lines[$i] -match "^\s*$([regex]::Escape($k))\s*=") { $lines[$i] = "$k=$($values[$k])"; $found = $true }
        }
        if (-not $found) { $lines.Insert($end, "$k=$($values[$k])"); $end++ }
    }
    [IO.File]::WriteAllLines($path, $lines, $enc)
}

function Clear-FovBinds([string]$path) {
    $enc   = [System.Text.Encoding]::Default
    $lines = [IO.File]::ReadAllLines($path, $enc)
    $changed = $false
    for ($i = 0; $i -lt $lines.Length; $i++) {
        if ($lines[$i] -match '^\s*(F9|F10)\s*=\s*FOV\b') { $lines[$i] = "$($Matches[1])="; $changed = $true }
    }
    if ($changed) { [IO.File]::WriteAllLines($path, $lines, $enc); Write-Host "Убраны бинды FOV в $(Split-Path -Leaf $path)" }
}

# Default.ini of the Russian release has struct values split at the commas ("Key=(X=1" / "Y=2" / "Z=3)"):
# glue them back. Gameplay physics ([Engine.PhysicsVolume]) is left exactly as the game ran so far.
function Repair-SplitLines([string]$path) {
    $enc = [System.Text.Encoding]::Default
    $src = [IO.File]::ReadAllLines($path, $enc)
    $out = [System.Collections.Generic.List[string]]::new()
    $sec = ''; $fixed = 0
    for ($i = 0; $i -lt $src.Length; $i++) {
        $l = $src[$i]; $t = $l.Trim()
        if ($t.StartsWith('[')) { $sec = $t }
        if ($sec -ine '[Engine.PhysicsVolume]' -and $t -match '^;?[A-Za-z0-9_]+=\(' -and -not $t.Contains(')')) {
            $j = $i + 1; $parts = @($l.TrimEnd())
            while ($j -lt $src.Length -and $src[$j].Trim() -match '^[A-Za-z]+=[^=\[\]]*$') {
                $parts += $src[$j].Trim(); if ($src[$j].Contains(')')) { break }; $j++
            }
            if ($j -lt $src.Length -and $src[$j].Contains(')') -and $parts.Count -gt 1) { $out.Add($parts -join ','); $i = $j; $fixed++; continue }
        }
        if ($t -like ';these can be...*' -and $t -notmatch ',') {
            $j = $i + 1; $parts = @($t)
            while ($j -lt $src.Length -and $src[$j].Trim() -match '^[A-Za-z]+$') { $parts += $src[$j].Trim(); $j++ }
            if ($parts.Count -gt 1) { $out.Add($parts -join ', '); $i = $j - 1; $fixed++; continue }
        }
        $out.Add($l)
    }
    if ($fixed) { [IO.File]::WriteAllLines($path, $out, $enc); Write-Host "Исправлены разорванные строки в $(Split-Path -Leaf $path): $fixed" }
}

# language switch (subtitles / voices / localized textures) is done by the launcher
$launcher = Join-Path $root 'Shrek 2 Enhanced Edition.exe'
if (-not (Test-Path $launcher)) { $launcher = Join-Path $root 'Shrek 2 Remaster.exe' }      # before 0.9.31
function Invoke-Launcher([string]$arg) {
    if (-not (Test-Path $launcher)) { return }
    $p = Start-Process -FilePath $launcher -ArgumentList $arg -Wait -PassThru
    if ($p.ExitCode -ne 0) {
        $e = Join-Path $here 'lang_error.txt'
        $msg = if (Test-Path $e) { Get-Content $e -Raw } else { "код $($p.ExitCode)" }
        Write-Host "Язык: $msg" -ForegroundColor Yellow
    }
}

if ($Uninstall) {
    # voices, subtitles, localized textures and the commercial back to the Russian originals
    Invoke-Launcher '--lang-reset'
    Write-Host 'Язык: возвращены русские озвучка, субтитры и текстуры'
    foreach ($k in $map.Keys) {
        $o = Join-Path $orig $k
        $d = Join-Path $root $map[$k]
        if (Test-Path $o) { Copy-Item $o $d -Force; Write-Host "Восстановлен оригинал $($map[$k])" }
        elseif (Test-Path $d) { Remove-Item $d; Write-Host "Удалён $($map[$k])" }
    }
    # level fixes (fog, lighting): the original maps come back from the backup
    if (Test-Path (Join-Path $backup 'Maps')) {
        Get-ChildItem (Join-Path $backup 'Maps') -Filter *.unr | ForEach-Object { Copy-Item $_.FullName (Join-Path $root "Maps\$($_.Name)") -Force; Write-Host "Восстановлен Maps\$($_.Name)" }
    }
    foreach ($rel in $iniFiles) {
        $b = Join-Path $backup $rel
        if (Test-Path $b) { Copy-Item $b (Join-Path $root $rel) -Force; Write-Host "Восстановлен $rel" }
    }
    if (Test-Path $docBackup) { Copy-Item $docBackup $docUser -Force; Write-Host "Восстановлен $docUser" }
    $foreignPad = Join-Path $backup 'System\dinput8.dll'
    if (Test-Path $foreignPad) { Copy-Item $foreignPad (Join-Path $system 'dinput8.dll') -Force; Write-Host 'Восстановлен прежний System\dinput8.dll' }
    # menu fonts are built by shrz.dll for the screen height on the first start
    foreach ($f in @('Textures\SHHDFonts.utx', 'Textures\SHHDFonts.tmp')) { $p = Join-Path $root $f; if (Test-Path $p) { Remove-Item $p; Write-Host "Удалён $f" } }
    $foreignAl = Join-Path $backup 'System\OpenAL32.dll'
    if (Test-Path $foreignAl) { Copy-Item $foreignAl (Join-Path $system 'OpenAL32.dll') -Force; Write-Host 'Восстановлен прежний System\OpenAL32.dll' }
    foreach ($f in @('ShrekPad.ini', 'ShrekPad.log', 'ShrekRender.ini', 'ShrekRender.log', 'ShrekRender_shaders.bin', 'ShrekRender_profile.txt', 'ShrekRender_frame.txt', 'ShrekRender_frame.bmp', 'ShrekRender_shaders.txt', 'alsoft.ini')) { $p = Join-Path $system $f; if (Test-Path $p) { Remove-Item $p; Write-Host "Удалён System\$f" } }
    if (-not (Test-Path (Join-Path $backup 'System\dxvk.conf')) -and (Test-Path (Join-Path $system 'dxvk.conf'))) {
        Remove-Item (Join-Path $system 'dxvk.conf'); Write-Host 'Удалён System\dxvk.conf'
    }
    if (Test-Path $layers) {
        $v = (Get-ItemProperty $layers -Name $exe -ErrorAction SilentlyContinue).$exe
        if ($v) {
            $nv = ($v -replace 'HIGHDPIAWARE', '').Trim()
            if ($nv -eq '~' -or $nv -eq '') { Remove-ItemProperty $layers -Name $exe } else { Set-ItemProperty $layers -Name $exe -Value $nv }
        }
    }
    Write-Host 'Готово: оригинальные файлы возвращены.'
    return
}

# 1. ini backups (first install only). An older borderless install kept Default.ini flat in backup\.
foreach ($rel in $iniFiles) {
    $src = Join-Path $root $rel
    $b   = Join-Path $backup $rel
    $old = Join-Path $backup (Split-Path -Leaf $rel)
    if ($rel -like '*dxvk.conf' -and (Test-Path $src) -and (Select-String -Path $src -Pattern 'ShrekRemaster' -Quiet)) { continue }
    if (-not (Test-Path $b)) {
        New-Item -ItemType Directory -Force (Split-Path -Parent $b) | Out-Null
        if (Test-Path $old) { Copy-Item $old $b }
        elseif (Test-Path $src) { Copy-Item $src $b }
    }
}

if ((Test-Path $docUser) -and -not (Test-Path $docBackup)) {
    New-Item -ItemType Directory -Force (Split-Path -Parent $docBackup) | Out-Null
    Copy-Item $docUser $docBackup
}

# 2. files
# 0.9.31: the project is "Shrek 2 Enhanced Edition"; the old launcher and scripts go away
if (Test-Path (Join-Path $root 'Shrek 2 Enhanced Edition.exe')) {
    foreach ($f in @((Join-Path $root 'Shrek 2 Remaster.exe'), (Join-Path $system 'Install-ShrekRemaster.bat'), (Join-Path $system 'Uninstall-ShrekRemaster.bat'))) {
        if (Test-Path $f) { Remove-Item $f -Force -ErrorAction SilentlyContinue; if (-not (Test-Path $f)) { Write-Host "Удалён старый $(Split-Path -Leaf $f)" } }
    }
}
# another mod's dinput8.dll is kept in backup (ours is recognised by its "ShrekPad" text)
$sysPad = Join-Path $system 'dinput8.dll'
if ((Test-Path $sysPad) -and -not (Select-String -Path $sysPad -Pattern 'ShrekPad' -Encoding Unicode -Quiet) -and -not (Select-String -Path $sysPad -Pattern 'ShrekPad' -Quiet)) {
    New-Item -ItemType Directory -Force (Join-Path $backup 'System') | Out-Null
    Copy-Item $sysPad (Join-Path $backup 'System\dinput8.dll') -Force
    Write-Host 'Прежний System\dinput8.dll сохранён в резервную копию'
}
# an OpenAL32.dll that is not ours (another size than the shipped OpenAL Soft) is kept in backup
$sysAl = Join-Path $system 'OpenAL32.dll'
if ((Test-Path $sysAl) -and -not (Test-Path (Join-Path $backup 'System\OpenAL32.dll')) -and ((Get-Item $sysAl).Length -ne (Get-Item (Join-Path $files 'OpenAL32.dll')).Length)) {
    New-Item -ItemType Directory -Force (Join-Path $backup 'System') | Out-Null
    Copy-Item $sysAl (Join-Path $backup 'System\OpenAL32.dll') -Force
    Write-Host 'Прежний System\OpenAL32.dll сохранён в резервную копию'
}
foreach ($k in $map.Keys) {
    $dst = Join-Path $root $map[$k]
    New-Item -ItemType Directory -Force (Split-Path -Parent $dst) | Out-Null
    Copy-Item (Join-Path $files $k) $dst -Force
    Write-Host "Установлен $($map[$k])"
}

# 2a. level fixes: fog on the beanstalk levels, lit furniture in the potion factory.
# The original map is kept in backup\Maps the first time (ours is recognised by its size).
$mapSrc = Join-Path $files 'Maps'
if (Test-Path $mapSrc) {
    New-Item -ItemType Directory -Force (Join-Path $backup 'Maps') | Out-Null
    Get-ChildItem $mapSrc -Filter *.unr | ForEach-Object {
        $dst = Join-Path $root "Maps\$($_.Name)"; $b = Join-Path $backup "Maps\$($_.Name)"
        if (-not (Test-Path $dst)) { return }
        if (-not (Test-Path $b) -and (Get-Item $dst).Length -ne $_.Length) { Copy-Item $dst $b }
        if (Test-Path $b) { Copy-Item $_.FullName $dst -Force; Write-Host "Установлен Maps\$($_.Name)" }
    }
}

# 2b. MSAA sample count lives in D3DDrv.dll after the marker 'SHRMSAA:'
$d3d = Join-Path $system 'D3DDrv.dll'
$bytes = [IO.File]::ReadAllBytes($d3d)
$marker = [Text.Encoding]::ASCII.GetBytes('SHRMSAA:')
$pos = -1
for ($i = 0; $i -le $bytes.Length - $marker.Length - 4; $i++) {
    $ok = $true
    for ($j = 0; $j -lt $marker.Length; $j++) { if ($bytes[$i + $j] -ne $marker[$j]) { $ok = $false; break } }
    if ($ok) { $pos = $i + $marker.Length; break }
}
if ($pos -lt 0) { throw 'D3DDrv.dll: метка MSAA не найдена' }
[BitConverter]::GetBytes([int]$Msaa).CopyTo($bytes, $pos)
[IO.File]::WriteAllBytes($d3d, $bytes)
Write-Host $(if ($Msaa -ge 2) { "Сглаживание: MSAA ${Msaa}x" } else { 'Сглаживание: выключено' })

foreach ($rel in @('System\Default.ini', 'System\Shrek2.ini')) { $p = Join-Path $root $rel; if (Test-Path $p) { Repair-SplitLines $p } }

# 3. physical resolution of the primary display
Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices;
public static class Disp {
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
$sz = [Disp]::Size()
Write-Host "Разрешение экрана: $($sz[0])x$($sz[1])"

# 4. ini: borderless window at native resolution; remove old F9/F10 FOV binds
foreach ($rel in @('System\Default.ini', 'System\User.ini', 'System\Shrek2.ini')) {
    $p = Join-Path $root $rel
    if (Test-Path $p) {
        Set-IniValues $p 'WinDrv.WindowsClient' @{
            StartupFullscreen = 'False'
            WindowedViewportX = $sz[0]; WindowedViewportY = $sz[1]
            FullscreenViewportX = $sz[0]; FullscreenViewportY = $sz[1]
            MenuViewportX = $sz[0]; MenuViewportY = $sz[1]
            TextureDetailInterface = 'UltraHigh'; TextureDetailTerrain = 'UltraHigh'; TextureDetailWeaponSkin = 'UltraHigh'
            TextureDetailPlayerSkin = 'UltraHigh'; TextureDetailWorld = 'UltraHigh'; TextureDetailRenderMap = 'UltraHigh'
        }
    }
}
foreach ($rel in @('System\Default.ini', 'System\User.ini', 'System\Shrek2.ini')) {
    $p = Join-Path $root $rel
    if (Test-Path $p) {
        # 16x anisotropic filtering; frame pacing is handled by DXVK (maxFrameLatency) instead of
        # ReduceMouseLag, which stalls the GPU by locking the back buffer every frame
        # detail textures at full sharpness up close (the 2004 default 0.8 blurred them to hide shimmering, which
        # anisotropic filtering now handles)
        Set-IniValues $p 'D3DDrv.D3DRenderDevice' @{ LevelOfAnisotropy = '16'; ReduceMouseLag = 'False'; DetailTexMipBias = '0.000000' }
    }
}
foreach ($rel in @('System\Default.ini', 'System\Shrek2.ini')) {
    $p = Join-Path $root $rel
    if (Test-Path $p) {
        # the engine's memory cache: 32 MB was sized for 2004 and is too tight for the HD textures
        Set-IniValues $p 'Engine.GameEngine' @{ CacheSizeMegs = '512' }
        # sound through OpenAL Soft (System\OpenAL32.dll; UseDefaultDriver=True would load the 2004
        # DefOpenAL32.dll): 3D positioning, EAX environments, 128 voices
        # UsePrecache stays off, as in the original: with it on the characters' mouths did not move (0.9.62)
        Set-IniValues $p 'ALAudio.ALAudioSubsystem' @{ UseDefaultDriver = 'False'; Use3DSound = 'True'; UseEAX = 'True'; Channels = '128'; UsePrecache = 'False' }
    }
}
$alsoft = Join-Path $system 'alsoft.ini'
if (-not (Test-Path $alsoft)) {
    [IO.File]::WriteAllLines($alsoft, @(
        '# Shrek 2 Enhanced Edition: OpenAL Soft settings (the launcher''s Sound tab writes them)', '',
        '[general]', 'stereo-mode=speakers', 'hrtf=false', 'frequency=48000', 'resampler=bsinc24', 'sources=256', 'output-limiter=true', '',
        '[eax]', 'enable=true'))
}
Write-Host 'Звук: OpenAL Soft (объёмный звук, эффекты среды EAX, 128 голосов, звуки уровня загружаются заранее); кеш движка 512 МБ'
$conf = Join-Path $system 'dxvk.conf'
$lines = [System.Collections.Generic.List[string]]::new()
if (Test-Path $conf) { foreach ($l in [IO.File]::ReadAllLines($conf)) { if ($l -notmatch '^\s*d3d9\.(samplerAnisotropy|maxFrameLatency|useD32forD24)\s*=' -and $l -notmatch 'ShrekRemaster') { $lines.Add($l) } } }
$lines.Add('# ShrekRemaster')
$lines.Add('d3d9.samplerAnisotropy = 16')
$lines.Add('d3d9.maxFrameLatency = 1')
$lines.Add('d3d9.useD32forD24 = True')      # 32-bit float depth buffer, reversed by shrz.dll
[IO.File]::WriteAllLines($conf, $lines)
Write-Host 'Анизотропная фильтрация 16x, задержка кадра 1, буфер глубины 32 бита (dxvk.conf)'
$userInis = @(@((Join-Path $root 'System\User.ini'), $docUser) | Where-Object { Test-Path $_ })
foreach ($p in @((Join-Path $root 'System\DefUser.ini')) + $userInis) {
    if (Test-Path $p) { Clear-FovBinds $p }
}
function Get-IniValue([string]$path, [string]$section, [string]$key) {
    $sec = $null
    foreach ($l in [IO.File]::ReadAllLines($path, [System.Text.Encoding]::Default)) {
        $t = $l.Trim()
        if ($t.StartsWith('[')) { $sec = $t.Trim('[', ']'); continue }
        if ($sec -ieq $section -and $t -match "^$([regex]::Escape($key))\s*=(.*)$") { return $Matches[1].Trim() }
    }
    return $null
}
# gameplay FOV: "modern" 95 unless the player already chose a sane value; once (0.9.52) the old default
# 85 becomes 95 too (old builds left 106 from F9 = FOV 106)
$fovMark = Join-Path $here 'fov95.done'
foreach ($userIni in $userInis) {
    $cur = Get-IniValue $userIni 'Engine.PlayerController' 'DefaultFOV'
    $v = 0.0
    $bad = -not $cur -or -not [double]::TryParse($cur, [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$v) -or $v -gt 110 -or $v -lt 60
    if ($bad -or (-not (Test-Path $fovMark) -and [math]::Abs($v - 85) -lt 0.01)) {
        Set-IniValues $userIni 'Engine.PlayerController' @{ DefaultFOV = '95.000000'; DesiredFOV = '95.000000' }
        Write-Host 'Угол обзора: 95 (современный)'
    }
}
Set-Content -Path $fovMark -Value '1'
# mouse: no engine-side smoothing (it re-used the last movement on frames without mouse data,
# a fix for 125 Hz mice; with raw input every frame has fresh counts, smoothing only adds lag)
foreach ($userIni in $userInis) {
    $has = [IO.File]::ReadAllLines($userIni, [System.Text.Encoding]::Default) | Where-Object { $_.Trim() -ieq '[Engine.PlayerInput]' }
    if (-not $has) { [IO.File]::AppendAllText($userIni, "`r`n[Engine.PlayerInput]`r`n", [System.Text.Encoding]::Default) }
    Set-IniValues $userIni 'Engine.PlayerInput' @{ MouseSmoothingMode = '0' }
}
Write-Host 'Мышь: без сглаживания движка'
# FOV presets used to be on F6/F7/F8 (in the way of the developer menu F7 and flying F8): those binds
# go; Insert cycles the angles now (ShrekPad)
foreach ($p in @((Join-Path $root 'System\DefUser.ini')) + $userInis) {
    if (-not (Test-Path $p)) { continue }
    $set = @{}
    foreach ($k in @('F6', 'F7', 'F8')) {
        $cur = Get-IniValue $p 'Engine.Input' $k
        if ($cur -and $cur -match '^FOV\b') { $set[$k] = '' }
    }
    if ($set.Count) { Set-IniValues $p 'Engine.Input' $set; Write-Host "Убраны бинды FOV с F6-F8 в $(Split-Path -Leaf $p)" }
}
# gamepad (System\dinput8.dll = ShrekPad): settings file once, the player's tuning is kept on reinstall
$padIni = Join-Path $system 'ShrekPad.ini'
if (-not (Test-Path $padIni)) { Copy-Item (Join-Path $files 'ShrekPad.ini') $padIni }
# adaptive triggers: the firm defaults of 0.9.36-0.9.37 become the soft ones (values the player set stay)
if (Test-Path $padIni) {
    $t = [IO.File]::ReadAllText($padIni)
    $n = $t -replace '(?m)^TriggerR2=25(?=\r?$)', 'TriggerR2=10' -replace '(?m)^TriggerR2Point=40(?=\r?$)', 'TriggerR2Point=0' -replace '(?m)^TriggerL2=10(?=\r?$)', 'TriggerL2=5'
    if ($n -ne $t) { [IO.File]::WriteAllText($padIni, $n, [Text.Encoding]::Unicode); Write-Host 'Курки DualSense: мягкие значения по умолчанию' }
}
foreach ($rel in @('System\Default.ini', 'System\User.ini', 'System\Shrek2.ini')) {
    $p = Join-Path $root $rel
    if (Test-Path $p) { Set-IniValues $p 'WinDrv.WindowsClient' @{ UseJoystick = 'True' } }
}
# gamepad layout: only keys that are still empty, so own bindings survive a reinstall
$padBinds = [ordered]@{
    # the engine appends " Speed=<axis value>" to the binding and reads the first Speed=, so stick
    # bindings carry no Speed: movement scales the value by Invert (keys give 1200), the camera uses
    # SpeedBase like the arrow keys (frame-rate independent turning)
    JoyX = 'Axis aStrafe Invert=2400'; JoyY = 'Axis aBaseY Invert=-2400'
    # the arrow keys turn with SpeedBase=0.9 / 0.7 at value 1; a full stick sends 0.5 * CameraSpeed
    # right stick: turning like the arrow keys, looking up/down like the mouse (aLookUp)
    JoyR = 'Axis aTurn SpeedBase=8';  JoyU = 'Axis aLookUp SpeedBase=6 Invert=-1'
    Joy1 = 'Jump | BypassCutscene'       # A
    Joy2 = 'Use'                         # B
    Joy3 = 'Fire'                        # X
    Joy4 = 'SelectPotion 102'            # Y: use the selected potion
    Joy5 = 'CenterView'                  # LB
    Joy6 = 'UseCarriedInventoryItem'     # RB
    Joy7 = 'BypassCutscene'              # Back
    Joy8 = 'EscHandler'                  # Start
    Joy9 = 'CenterView'                  # L3
    Joy10 = 'CenterView'                 # R3
    Joy11 = 'Walking'                    # LT: walk while held
    Joy12 = 'Fire'                       # RT
    # D-pad: up = potion menu, right / left = next / previous potion (selected only), down free
    Joy13 = 'StopMoviePlayback'; Joy14 = 'SelectPotion 100'; Joy16 = 'SelectPotion 101'
}
foreach ($p in @((Join-Path $root 'System\DefUser.ini')) + $userInis) {
    if (-not (Test-Path $p)) { continue }
    $set = @{}
    # empty keys and the broken stick bindings of 0.9.7-0.9.9 are (re)written; anything else is the player's own
    $old = @('Axis aStrafe Speed=2400', 'Axis aBaseY Speed=-2400', 'Axis aTurn Speed=21600', 'Axis aLookUp Speed=-21600',
             'Axis aTurn SpeedBase=54', 'Axis aLookUp SpeedBase=54 Invert=-1', 'Axis aTurn SpeedBase=4', 'Axis aLookUp SpeedBase=3 Invert=-1')
    foreach ($k in $padBinds.Keys) { $cur = Get-IniValue $p 'Engine.Input' $k; if (-not $cur -or $old -contains $cur) { $set[$k] = $padBinds[$k] } }
    if ($set.Count) { Set-IniValues $p 'Engine.Input' $set }
}
# potions (0.9.36): select and use apart. The old default layouts are moved to the new one; keys the
# player changed stay as they are.
foreach ($p in @((Join-Path $root 'System\DefUser.ini')) + $userInis) {
    if (-not (Test-Path $p)) { continue }
    $g = @{}; foreach ($k in 'Joy4', 'Joy13', 'Joy14', 'Joy15', 'Joy16', 'RightMouse', 'MouseWheelUp', 'MouseWheelDown', 'Q') { $g[$k] = Get-IniValue $p 'Engine.Input' $k }
    $set = @{}
    if ($g['Joy4'] -eq 'StopMoviePlayback' -and $g['Joy13'] -eq 'SelectPotion 0' -and $g['Joy14'] -eq 'SelectPotion 1' -and $g['Joy15'] -eq 'SelectPotion 2' -and $g['Joy16'] -eq 'SelectPotion 3') {
        $set['Joy4'] = 'SelectPotion 102'; $set['Joy13'] = 'StopMoviePlayback'; $set['Joy14'] = 'SelectPotion 100'; $set['Joy15'] = ''; $set['Joy16'] = 'SelectPotion 101'
    }
    if ($g['RightMouse'] -eq 'StopMoviePlayback') {     # the "modern" layout of the launcher
        $set['RightMouse'] = 'SelectPotion 102'
        if (-not $g['MouseWheelUp'] -or $g['MouseWheelUp'] -match 'NextSkill') { $set['MouseWheelUp'] = 'SelectPotion 101' }
        if (-not $g['MouseWheelDown'] -or $g['MouseWheelDown'] -match 'PrevSkill') { $set['MouseWheelDown'] = 'SelectPotion 100' }
        if (-not $g['Q'] -or $g['Q'] -eq 'WeaponZoom') { $set['Q'] = 'StopMoviePlayback' }
    }
    if ($set.Count) { Set-IniValues $p 'Engine.Input' $set; Write-Host "Зелья: новая раскладка ($(Split-Path -Leaf $p))" }
}
Write-Host 'Геймпад: System\dinput8.dll (XInput), настройки в System\ShrekPad.ini'
Write-Host 'Настройки обновлены'

# 5. DPI awareness (same as the checkbox in Compatibility)
if (-not (Test-Path $layers)) { New-Item $layers -Force | Out-Null }
$cur = (Get-ItemProperty $layers -Name $exe -ErrorAction SilentlyContinue).$exe
if (-not $cur) { $new = '~ HIGHDPIAWARE' } elseif ($cur -notmatch 'HIGHDPIAWARE') { $new = "$cur HIGHDPIAWARE" } else { $new = $cur }
Set-ItemProperty $layers -Name $exe -Value $new

# language: re-apply the player's choice (new translation files of this version, textures after an update)
Invoke-Launcher '--lang-apply'

Write-Host ''
Write-Host 'Готово. Запускайте игру как обычно.'
