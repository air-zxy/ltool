$ErrorActionPreference = 'Stop'
$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force }
Start-Sleep -Milliseconds 800
$null = Start-Process -FilePath $exe -PassThru
'waiting 30s for index...'
Start-Sleep -Seconds 30
1..4 | ForEach-Object {
  $null = Start-Process -FilePath $exe -ArgumentList '--show' -PassThru
  Start-Sleep -Seconds 3
}
Start-Sleep -Seconds 2
$log = Join-Path $env:APPDATA 'ltool\log.txt'
$pat = [string][char]0x9996 + [char]0x4f8b + [char]0x7ed8 + [char]0x5236   # "shou ci hui zhi"
$lines = Get-Content $log -Encoding UTF8
$hits = $lines | Where-Object { $_ -like 'show:*' -or $_ -like '*trim working*' }
$hits | Select-Object -Last 14 | ForEach-Object { $_ }
$ws = Get-Process ltool -ErrorAction SilentlyContinue
if ($ws) { 'working set now = ' + [int]($ws[0].WorkingSet64 / 1MB) + ' MB  private = ' + [int]($ws[0].PrivateMemorySize64 / 1MB) + ' MB' }
