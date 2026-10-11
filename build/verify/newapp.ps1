$ErrorActionPreference = 'Continue'
function Ch([int[]]$c) { $s = ''; foreach ($x in $c) { $s += [string][char]$x }; return $s }
$log = [System.Collections.Generic.List[string]]::new()
function Say([string]$s) { $log.Add($s); Write-Host ('| ' + $s) }
function Flush { [System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\newapp.txt', ($log -join "`n"), (New-Object System.Text.UTF8Encoding($false))) }
$appdata = Join-Path $env:APPDATA 'ltool'
$idx = Join-Path $appdata 'apps.json'
$lg = Join-Path $appdata 'log.txt'
function LogLines([string]$pattern) {
    # 只看这次运行新增的行，别删 log.txt（里面的崩溃时间线还要用）
    if (-not (Test-Path $lg)) { return @() }
    $all = @(Get-Content $lg)
    if ($all.Count -le $script:logBase) { return @() }
    return @($all[$script:logBase..($all.Count - 1)] | Where-Object { $PSItem -match $pattern })
}
function Read-Index {
    if (-not (Test-Path $idx)) { Say '  index file missing'; return @() }
    $txt = [IO.File]::ReadAllText($idx, [Text.Encoding]::UTF8)
    $arr = $null
    try { $arr = ConvertFrom-Json -InputObject $txt }
    catch { Say ('  PARSE FAILED len=' + $txt.Length + ' : ' + $_.Exception.Message); return @() }
    return @($arr)
}
function IndexCount() { return @(Read-Index).Count }
function IndexHas([string]$name) {
    foreach ($a in @(Read-Index)) { if ($a.name -eq $name) { return $true } }
    return $false
}

$progs = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs'
$lnkPath = Join-Path $progs 'Zz Qoder Watch Probe.lnk'
Say ('start menu programs = ' + $progs + '  exists=' + (Test-Path $progs))

Stop-Process -Name ltool -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
$script:logBase = @(Get-Content $lg -ErrorAction SilentlyContinue).Count
$p = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -PassThru
Say ('pid=' + $p.Id)

# wait for the initial scan + the watcher registration line
$seenWatch = $false
for ($i = 0; $i -lt 40; $i++) {
    Start-Sleep -Seconds 1
    if (@(LogLines 'apps: watching').Count -gt 0 -and @(LogLines 'scan finished').Count -gt 0) { $seenWatch = $true; break }
}
Say ('initial scan + watch registered = ' + $seenWatch)
foreach ($l in @(LogLines 'apps: watching|scan finished')) { Say ('  ' + $l) }
$base = IndexCount
Say ('index entries after startup = ' + $base)
Flush

# --- drop a brand new shortcut in, exactly like an installer would ---
$sh = New-Object -ComObject WScript.Shell
$sc = $sh.CreateShortcut($lnkPath)
$sc.TargetPath = 'C:\Windows\System32\notepad.exe'
$sc.WorkingDirectory = 'C:\Windows'
$sc.Description = 'probe'
$sc.Save()
[System.Runtime.InteropServices.Marshal]::ReleaseComObject($sh) | Out-Null
Say ('created ' + $lnkPath + '  exists=' + (Test-Path $lnkPath))

$found = $false
for ($i = 0; $i -lt 110; $i++) {
    Start-Sleep -Seconds 1
    if (IndexHas 'Zz Qoder Watch Probe') { $found = $true; Say ('  picked up after ' + ($i + 1) + 's'); break }
}
Say ('auto rescan picked up the new shortcut = ' + $found)
Say ('index entries now = ' + (IndexCount))
foreach ($l in @(LogLines 'apps: watching|scan finished')) { Say ('  ' + $l) }
Flush

# --- remove it and expect it to disappear from the index too ---
Remove-Item $lnkPath -Force
Say 'deleted the probe shortcut'
$gone = $false
for ($i = 0; $i -lt 110; $i++) {
    Start-Sleep -Seconds 1
    if (-not (IndexHas 'Zz Qoder Watch Probe')) { $gone = $true; Say ('  dropped from index after ' + ($i + 1) + 's'); break }
}
Say ('index dropped it = ' + $gone)
Say ('index entries now = ' + (IndexCount) + ' (baseline was ' + $base + ')')
foreach ($l in @(LogLines 'apps: watching|scan finished')) { Say ('  ' + $l) }
Flush

Stop-Process -Name ltool -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1
Start-Process -FilePath 'D:\software\ltool\ltool.exe'
Say 'restarted the installed ltool (the new build was only under test)'
Flush
Write-Host 'done'
