$ErrorActionPreference = 'Continue'
Get-CimInstance Win32_Process -Filter "Name='ltool.exe'" | Where-Object { $_.CommandLine -like '*--pinshot*' } | ForEach-Object {
  Write-Output ('收了测试实例 PID=' + $_.ProcessId)
  Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
}
Start-Sleep -Milliseconds 800
Get-CimInstance Win32_Process -Filter "Name='ltool.exe'" | ForEach-Object {
  Write-Output ('还在跑：PID ' + $_.ProcessId + '  ' + $_.ExecutablePath)
}
