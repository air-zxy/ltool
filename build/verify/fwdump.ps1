$ErrorActionPreference = 'Stop'
$f = New-Object -ComObject HNetCfg.FwPolicy2
Write-Output ('total rules = ' + $f.Rules.Count)
foreach ($r in $f.Rules) {
    $app = ''
    try { $app = $r.ApplicationName } catch { }
    if (($r.Name -like '*ltool*') -or ($app -like '*ltool*')) {
        Write-Output ('name=' + $r.Name + ' || app=' + $app + ' || dir=' + $r.Direction +
                      ' act=' + $r.Action + ' on=' + $r.Enabled)
    }
}
