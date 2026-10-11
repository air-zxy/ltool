$ErrorActionPreference = 'Stop'
$f = New-Object -ComObject HNetCfg.FwPolicy2
$paths = @()
foreach ($r in $f.Rules) {
    if ($r.ApplicationName -and $r.ApplicationName -match '^[A-Za-z]:\\') {
        $paths += ($r.ApplicationName + '|' + $r.Name + '|dir=' + $r.Direction + '|act=' + $r.Action)
    }
    if ($paths.Count -ge 4) { break }
}
$paths | Out-File -Encoding utf8 D:/project/cpp/ltool/build/verify/fwsample.txt
Write-Output ('sampled=' + $paths.Count)
