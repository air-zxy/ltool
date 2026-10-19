param([switch]$Quick, [switch]$Final, [switch]$NoBuild)
$ErrorActionPreference = 'Continue'
# One command for the whole delivery loop, timed per phase, so "what is slow here"
# is measured instead of guessed:
#   powershell -File build\verify\deliver.ps1              build + package(lzma2/normal 6.7s / 22.2MB) + install
#   powershell -File build\verify\deliver.ps1 -Quick       package with lzma2/fast (2.4s, installer 26.5MB)
#   powershell -File build\verify\deliver.ps1 -Final       package with lzma2/max (9.9s, 最小的 21.5MB)：出正式包用这个
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
  # --parallel 8：本机（20 逻辑核）全量重编实测 94.0s → 38.4s；16 只再快 2 秒，8 就到平台期了。
  # CI 那边是 2 核，压到 --parallel 4 + CL_MPcount=2，两边不是一回事，别互相抄。
  $b = & cmake --build "$root\build" --config Release --target ltool --parallel 8 2>&1
  $buildCode = $LASTEXITCODE
  $exe = Get-Item "$root\build\Release\ltool.exe"
  $errs = @($b | Where-Object { "$PSItem" -match ': error ' }).Count
  Phase 'build' $sw ('exe ' + $exe.LastWriteTime.ToString('HH:mm:ss') + '  ' + [int]($exe.Length / 1KB) + ' KB  errors=' + $errs)
  # 构建没过就停。以前这里只把 errors=N 打出来然后照样往下打包安装，
  # 于是"包出了、装了"，装上去的还是上一个 exe —— 假绿。
  if ($buildCode -ne 0 -or $errs -gt 0) {
    (($b | Select-Object -Last 25) -join [Environment]::NewLine) | Write-Host
    throw "构建没过（cmake 退出码 $buildCode，: error 行 $errs），不往下打包装了"
  }
}

# ---- core 的导出面就是已发布插件的 ABI：拿这份新 core 去对商店那批 dll 的导入表 ----
# 少一个符号 = 换上去当场 LoadLibrary 失败，而现场只报"加载失败"、不报缺哪个符号
# （2026-10-16 真踩过：删掉 Utils::launch / revealInExplorer 之后装的那份 9 颗起不来）。
# 商店产物在插件仓库的 dist\ 下；那个目录不在就**点名跳过**，不许当成通过。
$pub = 'C:\work\ltool_plugin\dist'
if (Test-Path $pub) {
  $abi = & python "$root\tools\abi_check.py" "$root\build\Release\ltoolcore.dll" $pub 2>&1
  $abiCode = $LASTEXITCODE
  # 数只能从 python 打的那行纯 ASCII 结论里取：按中文行数会数出 0（控制台 GBK 把 UTF-8
  # 中文搅成乱码，正则一条都不匹配 —— 实测就这么把"缺 7 个"报成了"缺 0 个"）。
  $missLine = ($abi | Select-String 'MISSING_SYMBOLS=(\d+)')
  $miss = if ($missLine) { [int]$missLine.Matches[0].Groups[1].Value } else { -1 }
  $note = if ($miss -lt 0) { ' -> 这一栏没数出来（python 没打 MISSING_SYMBOLS=），别当成通过' } `
          elseif ($abiCode -ne 0) { ' -> 发版前必须重发这批插件' } else { ' -> 都能加载' }
  Phase 'abi' $sw ('商店那批对这份 core：缺 ' + $miss + ' 个符号' + $note)
  if ($abiCode -ne 0) { (($abi | Select-String 'BAD|MISSING_SYMBOLS') -join [Environment]::NewLine) | Write-Host }
} else {
  Phase 'abi' $sw ('SKIP 没有 ' + $pub + ' —— 没查，不是通过')
}

# ---- 第二道：名字齐不等于能一起跑。比"这颗 dll 是什么时候编的"和"它吃的 core 头
# 之后有没有动过布局"（2026-10-20 他报"软件在磁盘清理闪退了"查出来的）。----
# `DiskScan::Tree` 加了两个 QString 之后大小和偏移全变了，而装着的 ltool-disk.dll 是加之前编的
# —— abi_check 那条只看导出名字，报"缺 0"、LoadLibrary 也真成功，然后运行期 0xc0000005。
# 所以这里数的是时间：dll 的 PE 编译时刻 vs 它 include 的那些头最后一次"动布局"的提交时刻。
# 装的那棵（D:\software\ltool\ltool-plugins）也一起查 —— 崩的是它，不是商店 dist。
$installed = 'D:\software\ltool\ltool-plugins'
$targets = @()
if (Test-Path $pub) { $targets += $pub }
if (Test-Path $installed) { $targets += $installed }
if ($targets.Count -eq 0) {
  Phase 'stale' $sw 'SKIP 商店 dist 和安装目录都不在 —— 没查，不是通过'
} else {
  # 不写 PYTHONIOENCODING 的话 python 按控制台 GBK 输出，中文行会成乱码、
  # 下面按"陈"字挑行就挑不到（数字那行是 ASCII，所以判定本身不会错，只是点不了名）。
  $env:PYTHONIOENCODING = 'utf-8'
  $stale = & python "$root\tools\abi_stale.py" @targets 2>&1
  $staleCode = $LASTEXITCODE
  $stLine = ($stale | Select-String 'STALE_GATE_BAD=(\d+)')
  $stN = if ($stLine) { [int]$stLine.Matches[0].Groups[1].Value } else { -1 }
  $stNote = if ($stN -lt 0) { ' -> 这一栏没数出来（python 没打 STALE_GATE_BAD=），别当成通过' } `
            elseif ($stN -gt 0) { ' -> 这些插件按老布局编的，读新 core 的结构体会错位（会崩，不是加载失败）' } `
            else { ' -> 没有陈的' }
  Phase 'stale' $sw ('布局陈旧的插件 ' + $stN + ' 颗' + $stNote)
  if ($stN -gt 0) { (($stale | Select-String '陈|DiskScan|Clipboard|CaptureHistory|STALE_GATE') -join [Environment]::NewLine) | Write-Host }
}

$iscc = 'D:\software\InnoSetup6\ISCC.exe'
# 档位见 ltool.iss 里那张实测表：默认 normal（6.7 秒 / 22.2MB），-Quick 出快包，
# -Final 回到 max+solid 出最小体积的正式包（9.9 秒 / 21.5MB）
$isccArgs = @()
if ($Final) { $isccArgs += '/DFINAL' } elseif ($Quick) { $isccArgs += '/DQUICK' }
# 版本号只写在 ltool.iss 那一条 #define 里：脚本从那儿读，别再抄一遍
# （0.4.0 那版散在六个脚本 + README + iss 注释里，升一次版本要改七处，漏一处就是"装了个旧包"）
$appVer = ([regex]::Match((Get-Content "$root\ltool.iss" -Raw), '(?m)^#define MyAppVersion "([^"]+)"')).Groups[1].Value
$setupPath = "$root\installer\ltool-$appVer-setup.exe"
$before = if (Test-Path $setupPath) { (Get-Item $setupPath).LastWriteTime } else { [datetime]::MinValue }
$p = & $iscc @isccArgs "$root\ltool.iss" 2>&1
$isccCode = $LASTEXITCODE
$setup = Get-Item $setupPath
# ISCC 编译失败时旧安装包还躺在原地（那次 #elifdef 打错就是这样：当场 abort，
# 脚本却报"打包 39ms、体积没变"），所以退出码和时间戳两个都要查
if ($isccCode -ne 0 -or $setup.LastWriteTime -le $before) {
  (($p | Select-Object -Last 12) -join [Environment]::NewLine) | Write-Host
  throw "ISCC 退出码 $isccCode、安装包时间戳 $($setup.LastWriteTime.ToString('HH:mm:ss')) 没往前走 —— 包根本没打成"
}
Phase 'package' $sw ('setup ' + $setup.LastWriteTime.ToString('HH:mm:ss') + '  ' + [math]::Round($setup.Length / 1MB, 2) + ' MB  mode=' + $(if ($Final) { 'max' } elseif ($Quick) { 'fast' } else { 'normal' }))

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
# ltoolcore.dll 是硬导入：缺了**根本不是"某个功能坏了"，是整个程序起不来**（实测把 build 那份
# 藏掉，ltool.exe 直接 error while loading shared libraries）。所以它必须按哈希比 ——
# 只看文件在不在，会放过"装了旧的一份"。
$coreB = (Get-FileHash "$root\build\Release\ltoolcore.dll" -Algorithm SHA256).Hash
$coreI = if (Test-Path 'D:\software\ltool\ltoolcore.dll') { (Get-FileHash 'D:\software\ltool\ltoolcore.dll' -Algorithm SHA256).Hash } else { '' }
if ($coreB -ne $coreI) { $verdict += ('  !!! ltoolcore.dll 不对（build {0} inst {1}）' -f $coreB.Substring(0,8), $coreI.Substring(0,8)) }
# 插件目录：从 c43b82f「开箱零插件」起，安装包**不再内建任何插件 dll**，只建一个空目录，
# 运行时由插件商店按需拉。所以这里不能再拿本地构建去逐颗比哈希 —— 装好的那些是 CI 编的那份，
# 天生和 build\Release 里的不一样，逐颗比只会天天红一条假故障。
# 真正的不变量是：目录在不在（不在 = 包坏了，装完一个插件都放不进去）。
$plugDir = 'D:\software\ltool\ltool-plugins'
if (-not (Test-Path $plugDir)) { $verdict += '  !!! 装完没有 ltool-plugins 目录（零插件包也要建这个空目录）' }
$installed = @(Get-ChildItem "$plugDir\*.dll" -ErrorAction SilentlyContinue)
$built = @(Get-ChildItem "$root\build\Release\ltool-plugins\*.dll" -ErrorAction SilentlyContinue)
$same = 0
foreach ($i in $installed) {
  $b = $built | Where-Object { $_.Name -eq $i.Name } | Select-Object -First 1
  if ($b -and (Get-FileHash $i.FullName -Algorithm SHA256).Hash -eq (Get-FileHash $b.FullName -Algorithm SHA256).Hash) { $same++ }
}
$verdict += ('  plugins dir={0} 装着 {1} 颗（其中 {2} 颗和本机构建同一份，其余是商店拉的 CI 那份，本来就该不同）' -f `
  (Test-Path $plugDir), $installed.Count, $same)
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
