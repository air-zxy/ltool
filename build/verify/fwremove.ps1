$ErrorActionPreference = 'Stop'
$exe = 'D:/project/cpp/ltool/build/Release/ltool.exe'
$name = [string][char]0x006C + [char]0x0074 + [char]0x006F + [char]0x006F + [char]0x006C + ' ' `
        + [char]0x5141 + [char]0x8BB8 + ' ltool-selftest'
Write-Output ('rule name = ' + $name)
$a = @('--fwremove', $name)
$p = Start-Process -FilePath $exe -ArgumentList $a -Verb RunAs -PassThru
$p.WaitForExit()
Write-Output ('elevated child exit=0x' + ('{0:X8}' -f $p.ExitCode))
Start-Sleep -Milliseconds 500
$f = New-Object -ComObject HNetCfg.FwPolicy2
$left = 0
foreach ($r in $f.Rules) { if ($r.Name -eq $name) { $left++ } }
Write-Output ('rules with that name left = ' + $left)
Get-Content 'C:\Users\loukas\AppData\Roaming\ltool\log.txt' | Select-String 'firewall:' | Select-Object -Last 3
