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

# ---- core 的导出面就是插件 dll 的 ABI：拿这份新 core 去对**要装上去的那两棵** dll 的导入表 ----
# 少一个符号 = 换上去当场 LoadLibrary 失败，而现场只报"加载失败"、不报缺哪个符号
# （2026-10-16 真踩过：删掉 Utils::launch / revealInExplorer 之后装的那份 9 颗起不来）。
# 量的是 build\Release\ltool-plugins（这次构建那份）+ D:\software\ltool\ltool-plugins（装着的、
# 崩的就是它）。**不再量 C:\work\ltool_plugin\dist**：商店从 1.3.0 起读的是主仓库这次发版的资产
# （releases/download/v<版本>/plugins-index.json，19 颗 dll 和 core 同一次构建），
# 那批手工提交进 git 的 dist 已经不是交付渠道，继续量它只会每轮喊一次没人能修的狼。
$inst = 'D:\software\ltool\ltool-plugins'
$abiTargets = @("$root\build\Release\ltool-plugins")
if (Test-Path $inst) { $abiTargets += $inst }
$abi = & python "$root\tools\abi_check.py" "$root\build\Release\ltoolcore.dll" $abiTargets 2>&1
$abiCode = $LASTEXITCODE
# 数只能从 python 打的那行纯 ASCII 结论里取：按中文行数会数出 0（控制台 GBK 把 UTF-8
# 中文搅成乱码，正则一条都不匹配 —— 实测就这么把"缺 7 个"报成了"缺 0 个"）。
$missLine = ($abi | Select-String 'MISSING_SYMBOLS=(\d+)')
$miss = if ($missLine) { [int]$missLine.Matches[0].Groups[1].Value } else { -1 }
$note = if ($miss -lt 0) { ' -> 这一栏没数出来（python 没打 MISSING_SYMBOLS=），别当成通过' } `
        elseif ($abiCode -ne 0) { ' -> 这批 dll 装上去会加载失败' } else { ' -> 都能加载' }
Phase 'abi' $sw ('本机构建 + 装着的那两棵对这份 core：缺 ' + $miss + ' 个符号' + $note)
if ($abiCode -ne 0) { (($abi | Select-String 'BAD|MISSING_SYMBOLS') -join [Environment]::NewLine) | Write-Host }

# ---- 第二道：名字齐不等于能一起跑。比"这颗 dll 是什么时候编的"和"它吃的 core 头
# 之后有没有动过布局"（2026-10-20 他报"软件在磁盘清理闪退了"查出来的）。----
# `DiskScan::Tree` 加了两个 QString 之后大小和偏移全变了，而装着的 ltool-disk.dll 是加之前编的
# —— abi_check 那条只看导出名字，报"缺 0"、LoadLibrary 也真成功，然后运行期 0xc0000005。
# 所以这里数的是时间：dll 的 PE 编译时刻 vs 它 include 的那些头最后一次"动布局"的提交时刻。
$targets = @()
if (Test-Path "$root\build\Release\ltool-plugins") { $targets += "$root\build\Release\ltool-plugins" }
if (Test-Path $inst) { $targets += $inst }
if ($targets.Count -eq 0) {
  Phase 'stale' $sw 'SKIP 本机构建和安装目录都不在 —— 没查，不是通过'
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

# ---- 第三道：接口号该不该升。公开头动了布局却没升 LTOOL_PLUGIN_API_VERSION，
# 老插件就会通过所有版本闸然后按老偏移读新结构体（上面那道量的是"这颗 dll 陈不陈"，
# 这一道量的是"宿主有没有资格认出陈的"）。发版由 CI 拦，这里只点名。
$apiGate = & python "$root\tools\core_abi_gate.py" 2>&1
$apiHits = ($apiGate | Select-String 'ABI_API_GATE_HITS=(\d+)')
$apiMeta = ($apiGate | Select-String 'ABI_API_META_BAD=(\d+)')
$apiNow = ($apiGate | Select-String 'ABI_API_NOW=(\d+)')
$hitsN = if ($apiHits) { [int]$apiHits.Matches[0].Groups[1].Value } else { -1 }
$metaN = if ($apiMeta) { [int]$apiMeta.Matches[0].Groups[1].Value } else { -1 }
$nowS = if ($apiNow) { $apiNow.Matches[0].Groups[1].Value } else { '?' }
$apiNote = if ($hitsN -lt 0 -or $metaN -lt 0) { ' -> 这一栏没数出来，别当成通过' } `
           elseif ($hitsN + $metaN -gt 0) { ' -> 发版前必须升号/对齐（CI 那步会红）' } else { ' -> 号配得上公开面' }
Phase 'apigate' $sw ('API ' + $nowS + '：动了布局的头 ' + $hitsN + ' 个，Api 抄错的 plugin.json ' + $metaN + ' 份' + $apiNote)
if ($hitsN -gt 0 -or $metaN -gt 0) { (($apiGate | Select-String '动过布局|Api 对不上|ABI_API') -join [Environment]::NewLine) | Write-Host }

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
# 插件目录：从 c43b82f「开箱零插件」起，安装包**不再内建任何插件 dll**，只建一个空目录。
# 这里只验目录在不在（不在 = 包坏了，装完一个插件都放不进去）；装着的那 19 颗对不对，
# 由下面 plugins 那步逐颗对齐 —— 以前它只报个数，而 10-20 崩的就是"装着的那颗是老的"。
$plugDir = 'D:\software\ltool\ltool-plugins'
if (-not (Test-Path $plugDir)) { $verdict += '  !!! 装完没有 ltool-plugins 目录（零插件包也要建这个空目录）' }
Phase 'install' $sw $verdict

# ---- 装完之后，把装着的那 19 颗对齐到**刚编出来的这一份** ----
# 为什么这一步要进这条闭环（2026-10-20 磁盘清理闪退）：core 的公开面一动（结构体加成员、
# Utils 搬进 platform 缝），老插件 dll 要么 LoadLibrary 失败，要么更坏 —— 名字全齐、装载成功、
# 按老偏移读新结构体然后当场 0xc0000005。接口号升到 4 之后宿主会**拒绝**装载对不上号的那颗，
# 于是"某个功能坏了"变成"F1/F2 和全部卡片一起没声"。所以换 core 的那一轮必须同时换插件，
# 中间不留窗口 —— 以前这一步是我手工拷的，手工的那次就是漏的那次。
# 商店那边从此也只发"和 core 同一次构建"的那批（tools/make_plugin_index.py + Release 资产）：
# 本地这台和商店是同一个来源，不是两套规矩。
$builtDir = "$root\build\Release\ltool-plugins"
$plugs = @(Get-ChildItem "$builtDir\ltool-*.dll" -ErrorAction SilentlyContinue)
if ($plugs.Count -eq 0) {
  Phase 'plugins' $sw 'SKIP 本机构建里没有插件 dll（build 那步没产出来？）—— 没查，不是通过'
} elseif (-not (Test-Path $plugDir)) {
  Phase 'plugins' $sw ('SKIP 没有 ' + $plugDir + ' —— 没查，不是通过')
} else {
  $sameN = 0; $repN = 0; $missN = 0; $lockN = 0
  $locked = @()
  foreach ($b in $plugs) {
    $dst = Join-Path $plugDir $b.Name
    if (-not (Test-Path $dst)) { $missN++ }
    elseif ((Get-FileHash $dst -Algorithm SHA256).Hash -eq (Get-FileHash $b.FullName -Algorithm SHA256).Hash) {
      $sameN++; continue
    } else { $repN++ }
    try { Copy-Item $b.FullName $dst -Force -ErrorAction Stop }
    catch { $lockN++; $locked += $b.Name }
  }
  $builtIds = @($plugs | ForEach-Object { $_.Name -replace '^ltool-', '' -replace '\.dll$', '' })
  $installed = @(Get-ChildItem "$plugDir\ltool-*.dll" -ErrorAction SilentlyContinue)
  $extraIds = @($installed | Where-Object { $builtIds -notcontains ($_.Name -replace '^ltool-', '' -replace '\.dll$', '') })
  # 商店卸载排队留下的 <file>.old.<毫秒> 不是 .dll 后缀，PluginHost::scan 不认它，
  # 但它会一直躺在目录里 —— 数出来点名，别让它变成"这目录怎么文件比卡片多"。
  $oldLeft = @(Get-ChildItem "$plugDir\*.old.*" -ErrorAction SilentlyContinue).Count
  # **对齐之后再量一遍**：上面 abi / stale 那两栏量的是"装之前"那批（那才是崩的原因），
  # 这一栏要说的是"现在这台机器上跑的那 19 颗对不对"。不复查就得把"缺 6 个符号"那句
  # 留在最后一屏上当成结论 —— 而那已经是 40 秒前的状态了（界面和脚本都不许说这种话）。
  $stillOff = @()
  foreach ($b in $plugs) {
    $dst = Join-Path $plugDir $b.Name
    if (-not (Test-Path $dst)) { $stillOff += ($b.Name + '（没有）'); continue }
    if ((Get-FileHash $dst -Algorithm SHA256).Hash -ne (Get-FileHash $b.FullName -Algorithm SHA256).Hash) {
      $stillOff += $b.Name
    }
  }
  $reabi = & python "$root\tools\abi_check.py" "$root\build\Release\ltoolcore.dll" $plugDir 2>&1
  $reMissLine = ($reabi | Select-String 'MISSING_SYMBOLS=(\d+)')
  $reMiss = if ($reMissLine) { [int]$reMissLine.Matches[0].Groups[1].Value } else { -1 }
  $rest = & python "$root\tools\abi_stale.py" $plugDir 2>&1
  $reStaleLine = ($rest | Select-String 'STALE_GATE_BAD=(\d+)')
  $reStale = if ($reStaleLine) { [int]$reStaleLine.Matches[0].Groups[1].Value } else { -1 }
  $note = ('装着 {0} 颗：{1} 颗本来就是这份哈希、换成新的 {2} 颗、原来缺 {3} 颗；' -f `
            $installed.Count, $sameN, ($repN - $lockN), $missN)
  $note += ('对齐后再量：缺 ' + $reMiss + ' 个符号 / 布局陈旧 ' + $reStale + ' 颗' `
            + $(if ($stillOff.Count) { ' —— 还有 ' + $stillOff.Count + ' 颗没对上：' + ($stillOff -join ' ') } `
                 else { '（19 颗哈希都就是刚编的那份）' }))
  if ($extraIds.Count) { $note += ('；装机目录里多出、本机构建没有的：' + (($extraIds | ForEach-Object { $_.Name }) -join ' ')) }
  if ($lockN) { $note += ('；被占用没换成 ' + $lockN + ' 颗：' + ($locked -join ' ') + ' —— 这几颗还是老的，会崩不是加载失败') }
  if ($oldLeft) { $note += ('；另有 ' + $oldLeft + ' 个 .old.* 排队等下次启动删') }
  Phase 'plugins' $sw $note
}

# ---- 装好的那份 exe 再跑一遍插件判据（2026-10-19 立的不变量）----
# build 树里全绿不等于装上去那套能跑：10-19 就是靠这条抓到 6 颗商店 dll 对新 core 装载失败、
# F1/F2 当场死，而 build 树那 240 条一条没红。安装包 Excludes 掉 qoffscreen，
# 所以平台插件要从 build 树借 —— 这两个变量必须一起给，只给一个就是"没有可用平台插件"。
if (Test-Path "$root\build\Release\platforms\qoffscreen.dll") {
  $env:QT_QPA_PLATFORM = 'offscreen'
  $env:QT_QPA_PLATFORM_PLUGIN_PATH = "$root\build\Release\platforms"
  $pt = & 'D:\software\ltool\ltool.exe' --plugintest 2>&1 | Out-String
  $ptPass = ([regex]::Matches($pt, '(?m)^PASS')).Count
  $ptBad = ([regex]::Matches($pt, '(?m)^FAIL')).Count
  Phase 'installed' $sw ('装好的 ltool.exe 跑 --plugintest：' + $ptPass + ' 过 / ' + $ptBad + ' 红')
  if ($ptBad -gt 0) { (($pt -split "`n" | Select-String '^FAIL') -join [Environment]::NewLine) | Write-Host }
} else {
  Phase 'installed' $sw 'SKIP build\Release\platforms\qoffscreen.dll 不在 —— 没跑装好的那份，不是通过'
}

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
