$ErrorActionPreference = 'Stop'
$o = 'D:/project/cpp/ltool/build/verify/smoke.out.txt'
$e = 'D:/project/cpp/ltool/build/verify/smoke.err.txt'
$p = Start-Process -FilePath 'D:/software/ltool/ltool.exe' -ArgumentList '--smoke' `
    -PassThru -Wait -NoNewWindow -RedirectStandardOutput $o -RedirectStandardError $e
Write-Output ("exit=0x" + ('{0:X8}' -f $p.ExitCode))
Write-Output '--- stdout tail ---'
Get-Content $o -Tail 20
Write-Output '--- stderr tail ---'
Get-Content $e -Tail 10
