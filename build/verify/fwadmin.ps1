$ErrorActionPreference = 'Stop'
$exe = 'D:/project/cpp/ltool/build/Release/ltool.exe'
$o = 'D:/project/cpp/ltool/build/verify/fwallow2.txt'
# 提权跑我们自己的 --fwallow：验证"以管理员身份时 COM 直加"这条路通不通
$a = @('--fwallow', 'D:\project\cpp\ltool\build\Release\ltool.exe', 'ltool-selftest')
$p = Start-Process -FilePath $exe -ArgumentList $a -Verb RunAs -PassThru
$p.WaitForExit()
Write-Output ('elevated child exit=0x' + ('{0:X8}' -f $p.ExitCode))
Start-Sleep -Milliseconds 500
$f = New-Object -ComObject HNetCfg.FwPolicy2
$found = @()
foreach ($r in $f.Rules) {
    if ($r.Name -like 'ltool*') {
        $found += ($r.Name + ' | app=' + $r.ApplicationName + ' | dir=' + $r.Direction + ' | act=' + $r.Action + ' | on=' + $r.Enabled)
    }
}
Write-Output ('rules named ltool = ' + $found.Count)
$found | ForEach-Object { Write-Output ('  ' + $_) }
