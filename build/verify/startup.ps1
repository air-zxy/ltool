$ErrorActionPreference = 'Stop'
$o = 'D:/project/cpp/ltool/build/verify/startup.out.txt'
$p = Start-Process -FilePath 'D:/project/cpp/ltool/build/Release/ltool.exe' -ArgumentList '--startupscan' `
    -PassThru -Wait -NoNewWindow -RedirectStandardOutput $o
Write-Output ('exit=0x' + ('{0:X8}' -f $p.ExitCode))
$lines = [System.IO.File]::ReadAllLines($o, [System.Text.Encoding]::UTF8)
Write-Output ('lines=' + $lines.Count)
[System.IO.File]::WriteAllLines('D:/project/cpp/ltool/build/verify/startup.txt', $lines,
    (New-Object System.Text.UTF8Encoding($false)))
