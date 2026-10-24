$root='mig'; $userInis=@()
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
function Get-IniValue([string]$path, [string]$section, [string]$key) {
    $sec = $null
    foreach ($l in [IO.File]::ReadAllLines($path, [System.Text.Encoding]::Default)) {
        $t = $l.Trim()
        if ($t.StartsWith('[')) { $sec = $t.Trim('[', ']'); continue }
        if ($sec -ieq $section -and $t -match "^$([regex]::Escape($key))\s*=(.*)$") { return $Matches[1].Trim() }
    }
    return $null
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

Get-Content mig/System/DefUser.ini
