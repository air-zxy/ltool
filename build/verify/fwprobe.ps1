$ErrorActionPreference = 'Stop'
$exe = 'D:/project/cpp/ltool/build/Release/ltool.exe'
$o = 'D:/project/cpp/ltool/build/verify/fw.out.txt'
$p1 = Start-Process -FilePath $exe -ArgumentList '--fw' -PassThru -Wait -NoNewWindow -RedirectStandardOutput $o
Write-Output ('no-arg exit=0x' + ('{0:X8}' -f $p1.ExitCode))
Get-Content $o -Encoding UTF8 | Select-Object -First 2
$o2 = 'D:/project/cpp/ltool/build/verify/fw2.out.txt'
$p2 = Start-Process -FilePath $exe -PassThru -Wait -NoNewWindow -RedirectStandardOutput $o2 `
    -ArgumentList '--fw', 'D:\software\weixin\weixin.exe'
Write-Output ('weixin exit=0x' + ('{0:X8}' -f $p2.ExitCode))
Get-Content $o2 -Encoding UTF8 | Select-Object -First 2
