$ErrorActionPreference = 'Continue'
$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
$dst = 'D:\software\ltool\ltool.exe'
Write-Output ("build  {0}  {1}" -f (Get-FileHash $exe).Hash.Substring(0,12), (Get-Item $exe).LastWriteTime)
Write-Output ("inst   {0}  {1}" -f (Get-FileHash $dst).Hash.Substring(0,12), (Get-Item $dst).LastWriteTime)
Write-Output '--- 正在跑的 ltool（Path 看是哪一个 exe）---'
Get-CimInstance Win32_Process -Filter "Name='ltool.exe'" | ForEach-Object {
  Write-Output ("PID {0}  start {1}  path {2}" -f $_.ProcessId, $_.CreationDate, $_.ExecutablePath)
}
Write-Output '--- 最近的 Inno Setup 日志 ---'
Get-ChildItem $env:TEMP -Filter '*Inno Setup Log*' | Sort-Object LastWriteTime -Descending |
  Select-Object -First 2 | ForEach-Object {
    Write-Output ('### ' + $_.FullName + '  ' + $_.LastWriteTime)
    Get-Content $_.FullName -Tail 25
  }
