$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition (Get-Content -Raw 'D:\project\cpp\ltool\build\verify\grab_src.txt')
$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force }
Start-Sleep -Milliseconds 600
$p = Start-Process -FilePath $exe -ArgumentList $args[0] -PassThru
Start-Sleep -Seconds 6
"pid=$($p.Id) hasExited=$($p.HasExited)"
$h = [Grab5]::Biggest([uint32]$p.Id)
"hwnd=$h"
Get-Content "$env:APPDATA\ltool\log.txt" -Tail 6
