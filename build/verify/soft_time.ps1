$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName System.Windows.Forms
$appdata = Join-Path $env:APPDATA 'ltool'
$apps = ([IO.File]::ReadAllText((Join-Path $appdata 'apps.json'), [Text.Encoding]::UTF8) | ConvertFrom-Json)
Write-Output ('index entries = ' + $apps.Count)

# the 27 targets our software list currently matches (installed rows)
$kws = @('7-zip','7z','vlc','visual studio code','vscode','idea','intellij','clion','steam','cmake',
         'cc switch','ccswitch','codex++','workbuddy','zcode','uu远程','uuyc','virtualbox','trae','wps',
         'microsoft office','微信','weixin','wechat','qq','飞书','feishu','企业微信','wxwork','钉钉',
         'dingtalk','wegame','mumu','motrix','telegram','阿里云盘','alipan','夸克','quark','lx music')
$targets = @()
foreach ($a in $apps) {
    $hay = ($a.name + ' ' + $a.target).ToLower()
    foreach ($k in $kws) { if ($hay.Contains($k)) { $targets += $a.target; break } }
}
$targets = $targets | Select-Object -Unique
Write-Output ('matched targets = ' + $targets.Count)

function Time-It([string]$label, [scriptblock]$body, [int]$repeat) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    for ($i = 0; $i -lt $repeat; $i++) { & $body $i | Out-Null }
    $sw.Stop()
    Write-Output ('  ' + $label + ': total=' + [math]::Round($sw.Elapsed.TotalMilliseconds) + 'ms  avg=' + [math]::Round($sw.Elapsed.TotalMilliseconds / $repeat, 2) + 'ms')
}

Write-Output '--- cold pass (first touch of each exe) ---'
$sw = [Diagnostics.Stopwatch]::StartNew()
$got = 0
foreach ($t in $targets) {
    if (Test-Path $t) { $v = (Get-Item $t -ErrorAction SilentlyContinue).VersionInfo.FileVersion; if ($v) { $got++ } }
}
$sw.Stop()
Write-Output ('  fileVersion x' + $targets.Count + ' cold: ' + [math]::Round($sw.Elapsed.TotalMilliseconds) + 'ms  (versions found=' + $got + ')')

Write-Output '--- warm pass (same list again) ---'
$sw = [Diagnostics.Stopwatch]::StartNew()
foreach ($t in $targets) { if (Test-Path $t) { (Get-Item $t -ErrorAction SilentlyContinue).VersionInfo.FileVersion | Out-Null } }
$sw.Stop()
Write-Output ('  fileVersion x' + $targets.Count + ' warm: ' + [math]::Round($sw.Elapsed.TotalMilliseconds) + 'ms')

Write-Output '--- config.ini open+parse (what installDirFor does per row) ---'
$cfg = Join-Path $appdata 'config.ini'
Time-It 'read config.ini' { param($i) (Get-Item $cfg).VersionInfo | Out-Null } 1
$sw = [Diagnostics.Stopwatch]::StartNew()
for ($i = 0; $i -lt 21; $i++) { Get-Content $cfg -Raw | Out-Null }
$sw.Stop()
Write-Output ('  read config.ini x21: ' + [math]::Round($sw.Elapsed.TotalMilliseconds) + 'ms')
$sw = [Diagnostics.Stopwatch]::StartNew()
for ($i = 0; $i -lt 21; $i++) { Test-Path 'D:\' | Out-Null }
$sw.Stop()
Write-Output ('  drive exists x21: ' + [math]::Round($sw.Elapsed.TotalMilliseconds) + 'ms')
