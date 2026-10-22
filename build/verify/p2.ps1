$ErrorActionPreference = 'Stop'
$out = 'D:/project/cpp/ltool/build/verify/p2.txt'
[void](Start-Process -FilePath 'D:/project/cpp/ltool/build/Release/ltool.exe' -ArgumentList '--pintest' `
    -RedirectStandardOutput $out)
Start-Sleep -Seconds 7
Write-Output '--- PINDBG ---'
Get-Content "$env:APPDATA\ltool\log.txt" | Select-String 'PINDBG' | Select-Object -Last 8
Write-Output '--- stdout ---'
Get-Content $out
