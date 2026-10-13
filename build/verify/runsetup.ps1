$ErrorActionPreference = 'Continue'
# bash 会把 /VERYSILENT 这类参数当路径转换（这次就被改成了 D:/software/Git/VERYSILENT，
# 于是 Setup 弹了向导界面卡在页面上），所以安装一定要从 .ps1 里起，别从 bash 起。
Get-CimInstance Win32_Process | Where-Object { $PSItem.Name -like 'ltool-*-setup*' } | ForEach-Object {
  Write-Output ('收掉卡住的 Setup PID=' + $PSItem.ProcessId)
  Stop-Process -Id $PSItem.ProcessId -Force -ErrorAction SilentlyContinue
}
Start-Sleep -Milliseconds 1200
$exe = (Get-ChildItem 'D:\project\cpp\ltool\installer\ltool-*-setup.exe' | Sort-Object LastWriteTime -Descending | Select-Object -First 1).FullName
$p = Start-Process -FilePath $exe -ArgumentList @('/VERYSILENT', '/NORESTART', '/SUPPRESSMSGBOXES', '/DIR=D:\software\ltool') -PassThru
if (-not $p.WaitForExit(420000)) { Write-Output 'Setup 7 分钟没结束（大概在等他退出正在用的 ltool）' }
Write-Output ('setup exit=' + $p.ExitCode)
Get-Item 'D:\software\ltool\ltool.exe' | ForEach-Object { Write-Output ('inst exe  ' + $_.LastWriteTime + '  ' + $_.Length) }
Get-Item 'D:\project\cpp\ltool\build\Release\ltool.exe' | ForEach-Object { Write-Output ('build exe ' + $_.LastWriteTime + '  ' + $_.Length) }
