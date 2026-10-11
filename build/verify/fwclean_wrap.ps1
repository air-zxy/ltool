$ErrorActionPreference = 'Stop'
$inner = 'D:/project/cpp/ltool/build/verify/fwclean_inner.ps1'
$p = Start-Process -FilePath 'powershell.exe' `
    -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $inner) `
    -Verb RunAs -PassThru -WindowStyle Hidden
$p.WaitForExit()
Write-Output ('elevated cleaner exit=0x' + ('{0:X8}' -f $p.ExitCode))
