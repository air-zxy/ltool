$ErrorActionPreference = 'Stop'
$exe = 'D:/project/cpp/ltool/build/Release/ltool.exe'
$o = 'D:/project/cpp/ltool/build/verify/fwtry.out.txt'
$a = @('--fwtry', 'D:\software\ltool\ltool.exe', 'ltool')
$p = Start-Process -FilePath $exe -ArgumentList $a -PassThru -Wait -NoNewWindow -RedirectStandardOutput $o
Write-Output ('exit=0x' + ('{0:X8}' -f $p.ExitCode))
$b = [System.IO.File]::ReadAllBytes($o)
Write-Output ('stdout bytes=' + $b.Length)
[System.IO.File]::WriteAllBytes('D:/project/cpp/ltool/build/verify/fwtry.txt', $b)
Get-Content 'D:/project/cpp/ltool/build/verify/fwtry.txt' -Encoding UTF8
$f = New-Object -ComObject HNetCfg.FwPolicy2
foreach ($r in $f.Rules) {
    if ($r.ApplicationName -like '*software\ltool*') {
        Write-Output ('RULE: ' + $r.Name + ' | dir=' + $r.Direction + ' act=' + $r.Action + ' on=' + $r.Enabled)
    }
}
Get-Content 'C:\Users\loukas\AppData\Roaming\ltool\log.txt' | Select-String 'firewall:' | Select-Object -Last 6
