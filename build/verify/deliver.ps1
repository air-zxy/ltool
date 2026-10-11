param([switch]$Quick, [switch]$NoBuild)
$ErrorActionPreference = 'Continue'
# One command for the whole delivery loop, timed per phase, so "what is slow here"
# is measured instead of guessed:
#   powershell -File build\verify\deliver.ps1              build + package(lzma2/max) + install + relaunch
#   powershell -File build\verify\deliver.ps1 -Quick       package with lzma2/fast (2.4s, installer +5MB)
#   powershell -File build\verify\deliver.ps1 -NoBuild     skip cmake --build
# ASCII-only where possible; the Chinese lines below need this file to carry a UTF-8
# BOM (node build\verify\addbom.js adds it) — PS 5.1 decodes a BOM-less file as GBK.
$root = 'D:\project\cpp\ltool'
# 控制台默认按 GBK 输出，log.txt 里那行中文会变成乱码（看着像日志坏了）
try { [Console]::OutputEncoding = New-Object Text.UTF8Encoding($false) } catch {}
$report = @()
function Phase($name, $sw, $extra) {
  # $script: 是必须的——函数里裸写 $report += 会建一个同名局部变量，
  # 整张计时表就一条都留不下来（实测：跑完 rc=0、stdout 0 字节，假绿）
  $script:report += ('{0,-8} {1,6} ms  {2}' -f $name, $sw.ElapsedMilliseconds, $extra)
  $sw.Restart()
}
$sw = [Diagnostics.Stopwatch]::StartNew()

if (-not $NoBuild) {
  $b = & cmake --build "$root\build" --config Release --target ltool 2>&1
  $exe = Get-Item "$root\build\Release\ltool.exe"
  Phase 'build' $sw ('exe ' + $exe.LastWriteTime.ToString('HH:mm:ss') + '  ' + [int]($exe.Length / 1KB) + ' KB  errors=' + @($b | Where-Object { "$PSItem" -match ': error ' }).Count)
}

$iscc = 'D:\software\InnoSetup6\ISCC.exe'
if ($Quick) { $p = & $iscc '/DQUICK' "$root\ltool.iss" 2>&1 } else { $p = & $iscc "$root\ltool.iss" 2>&1 }
$setup = Get-Item "$root\installer\ltool-0.4.0-setup.exe"
Phase 'package' $sw ('setup ' + $setup.LastWriteTime.ToString('HH:mm:ss') + '  ' + [math]::Round($setup.Length / 1MB, 2) + ' MB  mode=' + $(if ($Quick) { 'fast' } else { 'max' }))

& powershell -NoProfile -ExecutionPolicy Bypass -File "$root\build\verify\kill_ltool.ps1" | Out-Null
Phase 'stop' $sw 'old instance'

$inst = & powershell -NoProfile -ExecutionPolicy Bypass -File "$root\build\verify\install3.ps1" 2>&1
$bh = ''; $ih = ''
foreach ($l in $inst) {
  $t = "$l".Trim() -split '\s+'
  if ($t.Count -ge 4) {
    if ($t[0] -eq 'build') { $bh = $t[3] }
    if ($t[0] -eq 'inst') { $ih = $t[3] }
  }
}
$verdict = 'DIFF -> installed exe is NOT the build just packaged'
if ($bh -and $bh -eq $ih) { $verdict = 'MATCH ' + $bh.Substring(0, 12) }
# 7z 压缩的后端是 7zip.dll：漏打包不会让程序起不来（按需加载），但右键「压缩为 .7z」
# 就是坏的，而且界面只给一句"加载不了这个库"—— 所以装完必须点名确认它在。
if (-not (Test-Path 'D:\software\ltool\7zip.dll')) { $verdict += '  !!! 7zip.dll MISSING in install dir' }
Phase 'install' $sw $verdict

Start-Process -FilePath 'D:\software\ltool\ltool.exe'
$pid2 = 0
for ($i = 0; $i -lt 40; $i++) {
  $q = Get-Process ltool -ErrorAction SilentlyContinue | Select-Object -First 1
  if ($q) { $pid2 = $q.Id; break }
  Start-Sleep -Milliseconds 100
}
$log = ''
if (Test-Path "$env:APPDATA\ltool\log.txt") {
  $log = (Get-Content "$env:APPDATA\ltool\log.txt" -Tail 1 -Encoding UTF8) -join ''
}
Phase 'relaunch' $sw ('pid=' + $pid2 + '  last log: ' + $log)

foreach ($r in $report) { $r }
if ($report.Count -eq 0) {
  'NO PHASE RECORDED -> 这个脚本没干活，别当成跑过了'
  exit 1
}
'TOTAL    ' + (($report | ForEach-Object { [int](("$PSItem" -split '\s+')[1]) } | Measure-Object -Sum).Sum) + ' ms'
