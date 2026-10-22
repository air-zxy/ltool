$ErrorActionPreference = 'Stop'
$o = 'D:/project/cpp/ltool/build/verify/fwallow.out.txt'
$exe = 'D:/project/cpp/ltool/build/Release/ltool.exe'
$p = Start-Process -FilePath $exe -ArgumentList @('--fwallow', 'D:\project\cpp\ltool\build\Release\ltool.exe', 'ltool-selftest') `
    -PassThru -Wait -NoNewWindow -RedirectStandardOutput $o
Write-Output ('exit=0x' + ('{0:X8}' -f $p.ExitCode))
Get-Content $o -Encoding UTF8
Write-Output '--- log ---'
Get-Content "$env:APPDATA\ltool\log.txt" | Select-String 'firewall:' | Select-Object -Last 12
