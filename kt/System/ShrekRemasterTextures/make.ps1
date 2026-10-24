param([switch]$Uninstall, [ValidateSet('x4plus', 'x4plus-anime')][string]$Model = 'x4plus',
      [ValidateSet('x4plus', 'x4plus-anime')][string]$UiModel = 'x4plus-anime', [string]$Gpu = '', [switch]$NoUpscale)
$ErrorActionPreference = 'Stop'

$here   = Split-Path -Parent $MyInvocation.MyCommand.Path      # ...\System\ShrekRemasterTextures
$system = Split-Path -Parent $here
$root   = Split-Path -Parent $system
$texdir = Join-Path $root 'Textures'
$work   = Join-Path $here 'work'
$out    = Join-Path $here 'files'
$backup = Join-Path $here 'backup'
$tool   = Join-Path $here 'tools\ShrekTex.exe'
$esr    = Join-Path $here 'tools\realesrgan\realesrgan-ncnn-vulkan.exe'
$langdir = Join-Path $system 'ShrekRemaster\lang'
$texEn   = Join-Path $langdir 'tex_en.bin'          # English versions of signs, posters and menus (main patch)
$texEnHd = Join-Path $langdir 'tex_en_hd.bin'       # their HD versions, used by the launcher's English mode

# world textures: levels, characters, props
$world = @('1_Shreks_Swamp_Tex','2_Carriage_Hijack_Tex','3_The_Hunt_Tex','4_FGM_PIB_Tex','4_fgm_office_tx',
  '5_FGM_Donkey_Tex','6_Hamlet_Tex','7_Prison_Donkey_Tex','8_Prison_PIB_Tex','9_Prison_Shrek_Tex','10_Castle_Siege_Tex',
  '11_FGM_Battle_Tex','Ambush_TX','AmbCreaturesTX','Beanstalk','character_props_tx','EnvAnimsTx','PoisnAp_Tex',
  'SHCharacters','Shrek2_Karma_TX','Shrek2_Signs_TX','Shrek2_Univ_Tex','Swamp_Tex')
# interface: HUD, menu book, posters, potion menu and loading screens, storybook and its 2D animations
# (main patch 0.9.31 or newer: its scripts draw them at the authored size)
$ui = @('SH_Hud','SH_Menu','KWMenu','GUIContent','FGM_Potion_Menu','storybookanimTX','Storybook_TX','PIB_2Danims')
# fonts, palettes and terrain layer maps stay as they are
$packages = $world + $ui
$modelOf = @{}; foreach ($p in $world) { $modelOf[$p.ToLower()] = $Model }; foreach ($p in $ui) { $modelOf[$p.ToLower()] = $UiModel }

if (-not (Test-Path (Join-Path $system 'Game.exe'))) { throw "Game.exe не найден в $system. Распакуйте архив в папку игры." }
if (-not (Test-Path $texdir)) { throw "Папка Textures не найдена: $texdir" }

if ($Uninstall) {
    if (Test-Path $backup) {
        Get-ChildItem $backup -Filter *.utx | ForEach-Object { Copy-Item $_.FullName (Join-Path $texdir $_.Name) -Force; Write-Host "Восстановлен Textures\$($_.Name)" }
    }
    if (Test-Path $texEnHd) { Remove-Item $texEnHd -Force; Write-Host 'Удалены HD-версии английских текстур' }
    Write-Host 'Готово: оригинальные текстуры возвращены.'
    return
}

# The launcher's English mode appends English textures to some packages (lang\state.ini, [Overlay]:
# "base size header"). A backup must hold the original package: such a copy is cut back to it.
function Get-Overlays {
    $r = @{}; $st = Join-Path $langdir 'state.ini'
    if (-not (Test-Path $st)) { return $r }
    $sec = ''
    foreach ($l in [IO.File]::ReadAllLines($st)) {
        if ($l -match '^\s*\[(.+)\]\s*$') { $sec = $Matches[1]; continue }
        if ($sec -eq 'Overlay' -and $l -match '^([^=]+)=(\d+) (\d+) ([0-9a-fA-F]{128})\s*$') {
            $r[(Split-Path -Leaf $Matches[1]).ToLower()] = @{ Base = [int64]$Matches[2]; Size = [int64]$Matches[3]; Hdr = $Matches[4] }
        }
    }
    return $r
}
function Copy-Original([string]$src, [string]$dst, $ov) {
    $o = $ov[(Split-Path -Leaf $src).ToLower()]
    if ($o -and (Get-Item $src).Length -eq $o.Size) {
        $d = [IO.File]::ReadAllBytes($src)
        $b = New-Object byte[] $o.Base
        [Array]::Copy($d, $b, $o.Base)
        for ($i = 0; $i -lt 64; $i++) { $b[$i] = [Convert]::ToByte($o.Hdr.Substring($i * 2, 2), 16) }
        [IO.File]::WriteAllBytes($dst, $b)
        Write-Host "   $(Split-Path -Leaf $src): сохранён без английских текстур"
    } else { Copy-Item $src $dst }
}

# the game folder may contain non-Latin characters: run the tools with relative paths
Push-Location $root
try {
    $rel = { param($p) (Resolve-Path -Relative $p) }
    New-Item -ItemType Directory -Force (Join-Path $work 'lo'), (Join-Path $work 'hi'), $out, $backup | Out-Null

    # originals are backed up once, before anything is replaced
    $ov = Get-Overlays
    foreach ($p in $packages) {
        $src = Join-Path $texdir "$p.utx"; $b = Join-Path $backup "$p.utx"
        if ((Test-Path $src) -and -not (Test-Path $b)) { Copy-Original $src $b $ov }
    }
    $srcdir = & $rel $backup     # always read the originals, also when the HD set is already installed

    Write-Host '== 1/4  Извлечение текстур'
    & $tool extract $srcdir (& $rel $work) @packages
    if ($LASTEXITCODE) { throw 'ShrekTex extract завершился с ошибкой' }
    $haveEn = Test-Path $texEn
    if ($haveEn) {
        & $tool extract-patch (& $rel $texEn) (& $rel $work)
        if ($LASTEXITCODE) { throw 'ShrekTex extract-patch завершился с ошибкой' }
    }

    # the model for an image: by its package (EN__<package>__<texture> for the English versions)
    function Get-ModelFor([string]$name) {
        $n = $name -replace '^EN__', ''
        $i = $n.IndexOf('__'); if ($i -lt 0) { return $Model }
        $m = $modelOf[$n.Substring(0, $i).ToLower()]
        if ($m) { return $m } else { return $Model }
    }

    $lo = Join-Path $work 'lo'; $hi = Join-Path $work 'hi'
    $todo = @(Get-ChildItem $lo -Filter *.png | Where-Object { -not (Test-Path (Join-Path $hi $_.Name)) })
    if ($NoUpscale) {
        Write-Host '== 2/4  Апскейл пропущен (-NoUpscale): берутся готовые картинки из work\hi'
        # images without an upscale (new, or changed by this version of ShrekTex) go to work\lo_redo for chaiNNer
        $redo = Join-Path $work 'lo_redo'
        New-Item -ItemType Directory -Force $redo | Out-Null
        foreach ($f in $todo) { Copy-Item $f.FullName $redo -Force }
        if ($todo.Count -gt 0) {
            Write-Host ("   Нет апскейла для {0} картинок. Они скопированы в {1}:" -f $todo.Count, $redo) -ForegroundColor Yellow
            Write-Host '   прогоните эту папку через chaiNNer с выводом в work\hi и снова запустите Pack-HDTextures.bat.' -ForegroundColor Yellow
            Write-Host '   Пока что эти текстуры (и кубические карты, где они участвуют) останутся оригинальными.' -ForegroundColor Yellow
        }
        $todo = @()
    }
    if ($todo.Count -gt 0) {
        Write-Host "== 2/4  Апскейл нейросетью на видеокарте (мир: $Model, интерфейс: $UiModel)"
        $tmpIn = Join-Path $work 'batch'
        $n = 0; $total = $todo.Count
        foreach ($grp in ($todo | Group-Object { Get-ModelFor $_.BaseName })) {
            $m = $grp.Name; $items = @($grp.Group)
            # Real-ESRGAN processes a folder at once; feed it in batches so that progress is visible and a restart resumes
            for ($s = 0; $s -lt $items.Count; $s += 100) {
                if (Test-Path $tmpIn) { Remove-Item $tmpIn -Recurse -Force }
                New-Item -ItemType Directory $tmpIn | Out-Null
                $chunk = $items[$s..([Math]::Min($items.Count, $s + 100) - 1)]
                foreach ($f in $chunk) { Copy-Item $f.FullName $tmpIn }
                $esrArgs = @('-i', (& $rel $tmpIn), '-o', (& $rel $hi), '-n', "realesrgan-$m", '-s', '4', '-f', 'png')
                if ($Gpu) { $esrArgs += @('-g', $Gpu) }
                & $esr @esrArgs | Out-Null
                if ($LASTEXITCODE) { throw 'Real-ESRGAN завершился с ошибкой' }
                $n += $chunk.Count
                Write-Host ("   {0} / {1}  ({2})" -f $n, $total, $m)
            }
        }
        Remove-Item $tmpIn -Recurse -Force
    } elseif (-not $NoUpscale) { Write-Host '== 2/4  Апскейл: всё уже готово' }

    Write-Host '== 3/4  Сборка пакетов (мипы, сжатие DXT)'
    & $tool pack $srcdir (& $rel $work) (& $rel $out)
    if ($LASTEXITCODE) { throw 'ShrekTex pack завершился с ошибкой' }
    if ($haveEn) {
        $tmpEn = Join-Path $out 'tex_en_hd.bin'
        & $tool pack-patch (& $rel $texEn) (& $rel $work) (Join-Path (& $rel $out) 'tex_en_hd.bin')
        if ($LASTEXITCODE) { throw 'ShrekTex pack-patch завершился с ошибкой' }
    }

    Write-Host '== 4/4  Установка'
    foreach ($p in $packages) {
        $f = Join-Path $out "$p.utx"
        if (Test-Path $f) { Copy-Item $f (Join-Path $texdir "$p.utx") -Force; Write-Host "Установлен Textures\$p.utx" }
    }
    if ($haveEn -and (Test-Path $tmpEn)) {
        Copy-Item $tmpEn $texEnHd -Force
        Write-Host 'Установлены HD-версии английских текстур (лаунчер применит их при английском языке)'
    }
}
finally { Pop-Location }
Write-Host ''
Write-Host 'Готово. Запустите игру через лаунчер: он заново наложит английские текстуры, если выбран английский.'
Write-Host 'Папку work можно удалить (там промежуточные картинки), если не планируете пересобирать с другой моделью.'
