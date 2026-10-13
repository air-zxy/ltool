$ErrorActionPreference = 'Continue'
# 从 bash 直接起安装包会把 /VERYSILENT 这类参数被 MSYS 当路径转换掉（实测变成 D:/software/Git/VERYSILENT），
# Setup 就变成向导模式弹在用户屏幕上等点击。安装一律走这个脚本。
$root = 'D:\project\cpp\ltool'
# 版本号只写在 ltool.iss 那一条 #define 里：脚本从那儿读，别再抄一遍
# （0.4.0 那版散在六个脚本 + README + iss 注释里，升一次版本要改七处，漏一处就是"装了个旧包"）
$appVer = ([regex]::Match((Get-Content "$root\ltool.iss" -Raw), '(?m)^#define MyAppVersion "([^"]+)"')).Groups[1].Value
$stray = @(Get-CimInstance Win32_Process | Where-Object { $PSItem.Name -like 'ltool-*-setup*' })
foreach ($s in $stray) {
  Write-Output ('收掉没参数的 Setup PID=' + $s.ProcessId)
  Stop-Process -Id $s.ProcessId -Force -ErrorAction SilentlyContinue
}
# 只有真收到残留 Setup 才等（它在收尾/回滚，紧接着装会撞车）；平时这 1.5 秒是纯等
if ($stray.Count -gt 0) { Start-Sleep -Milliseconds 1500 }
$exe = "D:\project\cpp\ltool\installer\ltool-$appVer-setup.exe"
$argList = @('/VERYSILENT', '/NORESTART', '/SUPPRESSMSGBOXES', '/DIR=D:\software\ltool',
            '/LOG=D:\project\cpp\ltool\build\verify\setup3.log')
$p = Start-Process -FilePath $exe -ArgumentList $argList -PassThru
if (-not $p.WaitForExit(420000)) { Write-Output 'Setup 7 分钟没结束（可能在等退出正在运行的 ltool）' }
Write-Output ('setup exit=' + $p.ExitCode)
$b = Get-Item 'D:\project\cpp\ltool\build\Release\ltool.exe'
$i = Get-Item 'D:\software\ltool\ltool.exe'
Write-Output ('build ' + $b.LastWriteTime + ' ' + (Get-FileHash $b.FullName).Hash.Substring(0, 12))
Write-Output ('inst  ' + $i.LastWriteTime + ' ' + (Get-FileHash $i.FullName).Hash.Substring(0, 12))
