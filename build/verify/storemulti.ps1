param([string]$Flag = '--storemulti', [string]$Count = '3')
$ErrorActionPreference = 'Stop'
# 在一份**临时副本**里跑 --storemulti / --uninst（两面都会往运行目录真写删 dll）：那面自测会往「运行目录\ltool-plugins」真下 dll，
# 拿 build\Release 本尊跑会把本机测过的那份插件换成 CI 那份。
#   powershell -File build\verify\storemulti.ps1 [颗数]
# ltool.exe 是 GUI 子系统程序：PowerShell 里裸 `&` 调用**不会等它退出**（第一版就这么骗过去了一次，
# 后面 Remove-Item 才发现进程还在）。必须 Start-Process -Wait，并把 stdout 重定向到文件再读。
$src = 'D:\project\cpp\ltool\build\Release'
$dst = Join-Path $env:TEMP 'ltool-storemulti'
if (Test-Path $dst) { Remove-Item -Recurse -Force $dst }
New-Item -ItemType Directory -Force -Path $dst | Out-Null
Copy-Item -Recurse -Force (Join-Path $src '*') $dst
if (-not (Test-Path (Join-Path $dst 'ltool.exe'))) { 'FAIL: 副本里没有 ltool.exe'; exit 1 }
$out = Join-Path $dst 'selftest.txt'
$env:QT_QPA_PLATFORM = 'offscreen'   # Start-Process 起来的子进程继承这份环境
$p = Start-Process -FilePath (Join-Path $dst 'ltool.exe') -ArgumentList @($Flag, "$Count") `
     -WorkingDirectory $dst -WindowStyle Hidden -Wait -PassThru `
     -RedirectStandardOutput $out -RedirectStandardError (Join-Path $dst 'selftest.err')
Copy-Item -Force -ErrorAction SilentlyContinue $out 'D:/project/cpp/ltool/build/selftest-last.txt'
if (Test-Path $out) { Get-Content -Encoding UTF8 $out }
Copy-Item -Force $out 'D:\project\cpp\ltooluild\selftest-last.txt'   # 终端是 GBK，中文会花；留一份原件给人读
"rc=$($p.ExitCode)  （rc 非 0、或者上面没有那句「失败 N 项」= 它半路崩了）"
Remove-Item -Recurse -Force $dst
exit $p.ExitCode
