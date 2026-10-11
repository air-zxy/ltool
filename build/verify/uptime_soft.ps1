$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
$out = 'D:\project\cpp\ltool\build\verify\uptime_soft.out'
$p = Start-Process -FilePath $exe -ArgumentList '--uptime', 'soft' -Wait -PassThru -NoNewWindow -RedirectStandardOutput $out
"exit=0x" + ('{0:X8}' -f $p.ExitCode)
Get-Content $out -Encoding UTF8
"--- log tail ---"
Get-Content "$env:APPDATA\ltool\log.txt" -Tail 3 -Encoding UTF8
