# Windows PowerShell 5.1 misreads a default value followed by more parameters: defaults go last
param(
    [switch]$Uninstall,
    [switch]$PackAnyway,
    [switch]$Upscale,
    [switch]$NoMusic,
    [switch]$NoVoice,
    [int]$Steps = 50
)
$ErrorActionPreference = 'Stop'

$here   = Split-Path -Parent $MyInvocation.MyCommand.Path      # ...\System\ShrekRemasterAudio
$system = Split-Path -Parent $here
$root   = Split-Path -Parent $system
$sounds = Join-Path $root 'Sounds'
$music  = Join-Path $root 'Music'
$langd  = Join-Path $system 'ShrekRemaster\lang'
$work   = Join-Path $here 'work'
$out    = Join-Path $here 'files'
$backup = Join-Path $here 'backup'
$tool   = Join-Path $here 'tools\ShrekSnd.exe'
$launcher = Join-Path $root 'Shrek 2 Enhanced Edition.exe'

if (-not (Test-Path (Join-Path $system 'Game.exe'))) { throw "Game.exe не найден в $system. Распакуйте архив в папку игры." }
if (-not (Test-Path $sounds)) { throw "Папка Sounds не найдена: $sounds" }

# Sets of sound packages: where each lives in the game, which AudioSR model suits it.
#   sfx:   Sounds\*.uax (not the dialogue) + System\AmbientCreatures.u (creature sounds kept in a script package)
#   voice: the two voice packs the launcher's language switch links into Sounds\AllDialog*
$sets = [ordered]@{}
$sfxFiles = [ordered]@{}
Get-ChildItem $sounds -Filter *.uax | Where-Object { $_.Name -notlike 'AllDialog*' } | ForEach-Object { $sfxFiles[$_.Name] = $_.FullName }
if (Test-Path (Join-Path $system 'AmbientCreatures.u')) { $sfxFiles['AmbientCreatures.u'] = Join-Path $system 'AmbientCreatures.u' }
$sets['sfx'] = @{ Files = $sfxFiles; Model = 'basic'; Title = 'Эффекты' }
if (-not $NoVoice) {
    $voiceFiles = [ordered]@{}
    foreach ($v in 'voice_ru.uax', 'voice_en.uax') { $f = Join-Path $langd $v; if (Test-Path $f) { $voiceFiles[$v] = $f } }
    if ($voiceFiles.Count) { $sets['voice'] = @{ Files = $voiceFiles; Model = 'speech'; Title = 'Озвучка' } }
}
$bMusic = Join-Path $backup 'Music'

# the game keeps its sound files open while it runs: the files are replaced only
# once it is closed - the script waits for that instead of stopping
function Get-GameProcess {
    Get-Process -ErrorAction SilentlyContinue | Where-Object {
        $_.Name -eq 'Game' -and $_.Path -and $_.Path.StartsWith($root, [StringComparison]::OrdinalIgnoreCase) }
}
function Wait-GameClosed {
    $said = $false
    while (Get-GameProcess) {
        if (-not $said) { Write-Host 'Игра сейчас запущена — закройте её, установка продолжится сама...' -ForegroundColor Yellow; $said = $true }
        Start-Sleep -Seconds 2
    }
    if ($said) { Write-Host 'Игра закрыта, продолжаю.' }
}
function Install-File([string]$src, [string]$dst) {
    # a new file, not written into the old one: the voice packs are hard links (AllDialog), and
    # voice_ru may share its data with the launcher's untouched original
    for ($try = 1; ; $try++) {
        try {
            if (Test-Path $dst) { Remove-Item $dst -Force }
            Copy-Item $src $dst
            return
        } catch {
            if ($try -ge 60) { throw "Не удалось заменить $dst — файл занят другой программой. Закройте игру (и всё, что могло открыть её файлы) и запустите установку ещё раз." }
            if ($try -eq 1) { Write-Host "Файл $(Split-Path -Leaf $dst) занят — жду, пока его освободят (закройте игру)..." -ForegroundColor Yellow }
            Wait-GameClosed
            Start-Sleep -Seconds 2
        }
    }
}
function Relink-Voices {
    if (Test-Path $launcher) {
        Start-Process -FilePath $launcher -ArgumentList '--lang-apply' -Wait | Out-Null
        Write-Host 'Озвучка заново подключена лаунчером'
    } else { Write-Host 'Запустите лаунчер: он подключит новую озвучку' -ForegroundColor Yellow }
}

if ($Uninstall) {
    Wait-GameClosed
    foreach ($k in $sets.Keys) {
        $bdir = Join-Path $backup $k
        foreach ($name in $sets[$k].Files.Keys) {
            $b = Join-Path $bdir $name
            if (Test-Path $b) { Install-File $b $sets[$k].Files[$name]; Write-Host "Восстановлен $name" }
        }
    }
    # the first version of the kit kept the effects in backup\Sounds
    $old = Join-Path $backup 'Sounds'
    if (Test-Path $old) { Get-ChildItem $old -Filter *.uax | ForEach-Object { Copy-Item $_.FullName (Join-Path $sounds $_.Name) -Force; Write-Host "Восстановлен Sounds\$($_.Name)" } }
    if (Test-Path $bMusic) { Get-ChildItem $bMusic -Filter *.ogg | ForEach-Object { Copy-Item $_.FullName (Join-Path $music $_.Name) -Force; Write-Host "Восстановлен Music\$($_.Name)" } }
    if ($sets.Contains('voice')) { Relink-Voices }
    Write-Host 'Готово: оригинальный звук возвращён.'
    return
}

Push-Location $root
try {
    $rel = { param($p) (Resolve-Path -Relative $p) }
    $musLo = Join-Path $work 'music\lo'; $musHi = Join-Path $work 'music\hi'
    New-Item -ItemType Directory -Force $musLo, $musHi, $out, $bMusic | Out-Null

    # ---- originals, backed up once before anything is replaced
    $oldSfx = Join-Path $backup 'Sounds'
    foreach ($k in $sets.Keys) {
        $bdir = Join-Path $backup $k; New-Item -ItemType Directory -Force $bdir | Out-Null
        foreach ($name in $sets[$k].Files.Keys) {
            $b = Join-Path $bdir $name
            if (Test-Path $b) { continue }
            $prev = Join-Path $oldSfx $name                 # backup of the first version of the kit
            if (Test-Path $prev) { Copy-Item $prev $b } else { Copy-Item $sets[$k].Files[$name] $b }
        }
    }
    if (Test-Path $music) { Get-ChildItem $music -Filter *.ogg | ForEach-Object { $b = Join-Path $bMusic $_.Name; if (-not (Test-Path $b)) { Copy-Item $_.FullName $b } } }
    Get-ChildItem $bMusic -Filter *.ogg | ForEach-Object { $d = Join-Path $musLo $_.Name; if (-not (Test-Path $d)) { Copy-Item $_.FullName $d } }

    # ---- 1. sounds to WAV
    Write-Host '== 1/3  Извлечение звуков'
    $state = [ordered]@{}
    foreach ($k in $sets.Keys) {
        $w = Join-Path $work $k; $lo = Join-Path $w 'lo'; $hi = Join-Path $w 'hi'
        New-Item -ItemType Directory -Force $lo, $hi | Out-Null
        & $tool extract (& $rel (Join-Path $backup $k)) (& $rel $w) @($sets[$k].Files.Keys)
        if ($LASTEXITCODE) { throw 'ShrekSnd extract завершился с ошибкой' }
        $state[$k] = @{ Lo = $lo; Hi = $hi; Work = $w }
    }

    # upscalers often add a suffix or a dated subfolder (AudioSR: <name>_AudioSR_Processed_48K.wav):
    # such files are moved to hi under the original name
    function Collect([string]$from, [string]$lo, [string]$hi) {
        foreach ($f in @(Get-ChildItem $from -Filter *.wav -Recurse)) {
            $n = $f.BaseName -replace '(?i)_(AudioSR|processed|enhanced|upscaled)(_.*)?$', ''
            $dst = Join-Path $hi "$n.wav"
            if ($f.FullName -ne $dst -and (Test-Path (Join-Path $lo "$n.wav"))) { Move-Item $f.FullName $dst -Force }
        }
    }
    function Missing($s) { @(Get-ChildItem $s.Lo -Filter *.wav | Where-Object { -not (Test-Path (Join-Path $s.Hi $_.Name)) }) }
    foreach ($k in $state.Keys) { Collect $state[$k].Hi $state[$k].Lo $state[$k].Hi }
    $musTodo = @(Get-ChildItem $musLo -Filter *.ogg | Where-Object { -not (Test-Path (Join-Path $musHi $_.Name)) })
    $anyTodo = $musTodo.Count -gt 0 -and -not $NoMusic
    foreach ($k in $state.Keys) { if ((Missing $state[$k]).Count) { $anyTodo = $true } }

    # ---- 2. neural networks (button "Create")
    if ($Upscale -and $anyTodo) {
        # AudioSR (https://github.com/haoheliu/versatile_audio_super_resolution) and Apollo
        # (https://github.com/JusperLee/Apollo) in their own Python, set up here by uv: nothing is
        # installed system-wide, everything stays in ShrekRemasterAudio
        Write-Host '== Нейросети на видеокарте'
        $uv = Join-Path $here 'tools\uv.exe'; $venv = Join-Path $here 'audiosr'; $py = Join-Path $venv 'Scripts\python.exe'
        $cache = Join-Path $here 'cache'
        $env:UV_CACHE_DIR = Join-Path $cache 'uv'; $env:UV_PYTHON_INSTALL_DIR = Join-Path $cache 'python'; $env:HF_HOME = Join-Path $cache 'huggingface'
        $env:PYTHONIOENCODING = 'utf-8'
        $env:TORCH_FORCE_NO_WEIGHTS_ONLY_LOAD = '1'    # PyTorch 2.6+ refuses these checkpoint formats otherwise
        if (-not (Test-Path (Join-Path $venv 'ready.txt'))) {
            Write-Host '   Первый запуск: скачиваются Python, PyTorch (CUDA 12.8) и AudioSR, около 4 ГБ. Это один раз.'
            if (-not (Test-Path $py)) { & $uv venv --python 3.10 $venv; if ($LASTEXITCODE) { throw 'uv: не удалось создать окружение Python' } }
            & $uv pip install --python $py torch==2.7.1 torchvision==0.22.1 torchaudio==2.7.1 --index-url https://download.pytorch.org/whl/cu128
            if ($LASTEXITCODE) { throw 'uv: не удалось установить PyTorch' }
            & $uv pip install --python $py audiosr==0.0.7 'numpy<=1.23.5'
            if ($LASTEXITCODE) { throw 'uv: не удалось установить AudioSR' }
            & $py -c "import torch; print('PyTorch', torch.__version__, 'CUDA:', torch.cuda.is_available(), torch.cuda.get_device_name(0) if torch.cuda.is_available() else '')"
            Set-Content (Join-Path $venv 'ready.txt') 'ok'
        }
        # AudioSR's dependencies still import pkg_resources (setuptools up to 80) and modules its code
        # does not declare (matplotlib, scikit-learn, deepdiff; omegaconf for Apollo's checkpoint); the pins keep AudioSR's versions
        & $uv pip install --python $py 'setuptools<81' wheel matplotlib scikit-learn deepdiff omegaconf 'numpy<=1.23.5' 'transformers==4.30.2' 'librosa==0.9.2'
        if ($LASTEXITCODE) { throw 'uv: не удалось доустановить модули' }
        & $py -c "import pkg_resources, matplotlib, sklearn, soundfile, audiosr; from audiosr import build_model, super_resolution"
        if ($LASTEXITCODE) { throw 'AudioSR не загружается (см. сообщения выше)' }

        # effects first (the most audible), music, then the voices (the longest: several hours)
        if ($state.Contains('sfx') -and (Missing $state['sfx']).Count) {
            Write-Host '== Эффекты: AudioSR'
            $list = Join-Path $state['sfx'].Work 'todo.txt'
            [IO.File]::WriteAllLines($list, [string[]](Missing $state['sfx'] | ForEach-Object { $_.FullName }))
            & $py (Join-Path $here 'tools\run_audiosr.py') $list $state['sfx'].Hi $Steps basic
            if ($LASTEXITCODE) { Write-Host 'AudioSR завершился с ошибкой (см. выше): необработанные звуки остаются оригинальными, остальное продолжается' -ForegroundColor Yellow }
        }
        if ($musTodo.Count -gt 0 -and -not $NoMusic) {
            Write-Host '== Музыка: Apollo (стерео, 44 кГц)'
            & $py (Join-Path $here 'tools\run_apollo.py') $musLo $musHi
            # the music is optional: effects and voices go on, the tracks stay the original ones
            if ($LASTEXITCODE) { Write-Host 'Apollo завершился с ошибкой (см. выше): музыка пока остаётся оригинальной, остальное продолжается' -ForegroundColor Yellow }
        }
        if ($state.Contains('voice') -and (Missing $state['voice']).Count) {
            Write-Host '== Озвучка: AudioSR (модель для речи). Это долго - можно закрыть окно и продолжить потом'
            $list = Join-Path $state['voice'].Work 'todo.txt'
            [IO.File]::WriteAllLines($list, [string[]](Missing $state['voice'] | ForEach-Object { $_.FullName }))
            & $py (Join-Path $here 'tools\run_audiosr.py') $list $state['voice'].Hi $Steps speech
            if ($LASTEXITCODE) { Write-Host 'AudioSR завершился с ошибкой (см. выше): необработанные звуки остаются оригинальными, остальное продолжается' -ForegroundColor Yellow }
        }
    }

    foreach ($k in $state.Keys) {
        $n = @(Get-ChildItem $state[$k].Lo -Filter *.wav).Count
        Write-Host ("   {0}: {1} звуков, обработано {2}" -f $sets[$k].Title, $n, ($n - (Missing $state[$k]).Count))
    }
    Write-Host ("   Музыка: {0} треков, обработано {1}" -f @(Get-ChildItem $musLo -Filter *.ogg).Count, @(Get-ChildItem $musHi -Filter *.ogg -ErrorAction SilentlyContinue).Count)
    $blocked = $false
    foreach ($k in $state.Keys) { if ((Missing $state[$k]).Count) { $blocked = $true } }
    if ($blocked -and -not $PackAnyway) {
        Write-Host ''
        Write-Host 'Обработаны не все звуки. Нажмите «Создать» (нейросеть сделает остальное) или обработайте сами' -ForegroundColor Yellow
        foreach ($k in $state.Keys) { if ((Missing $state[$k]).Count) { Write-Host ("   {0}\lo -> {0}\hi (WAV, те же имена)" -f $state[$k].Work) -ForegroundColor Yellow } }
        Write-Host 'и снова «Упаковать». Сборка пропущена, установленный звук не менялся.' -ForegroundColor Yellow
        Write-Host '(Собрать сразу, оставив необработанные оригинальными: make.ps1 -PackAnyway)'
        return
    }

    # ---- 3. packages
    Write-Host '== 2/3  Сборка звуковых пакетов'
    foreach ($k in $state.Keys) {
        $o = Join-Path $out $k; New-Item -ItemType Directory -Force $o | Out-Null
        & $tool pack (& $rel (Join-Path $backup $k)) (& $rel $state[$k].Work) (& $rel $o)
        if ($LASTEXITCODE) { throw 'ShrekSnd pack завершился с ошибкой' }
    }

    Write-Host '== 3/3  Установка'
    Wait-GameClosed
    foreach ($k in $state.Keys) {
        foreach ($name in $sets[$k].Files.Keys) {
            $f = Join-Path (Join-Path $out $k) $name
            if (Test-Path $f) { Install-File $f $sets[$k].Files[$name]; Write-Host "Установлен $name" }
        }
    }
    Get-ChildItem $musHi -Filter *.ogg -ErrorAction SilentlyContinue | ForEach-Object {
        if (Test-Path (Join-Path $bMusic $_.Name)) { Install-File $_.FullName (Join-Path $music $_.Name); Write-Host "Установлен Music\$($_.Name)" }
        else { Write-Host "Пропущен $($_.Name): в игре нет такого трека" -ForegroundColor Yellow }
    }
    if ($state.Contains('voice')) { Relink-Voices }
}
finally { Pop-Location }
Write-Host ''
Write-Host 'Готово. Оригиналы лежат в ShrekRemasterAudio\backup (вернуть: Uninstall-HDAudio.bat или «Удалить» в лаунчере).'
