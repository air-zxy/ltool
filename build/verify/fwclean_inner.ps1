$ErrorActionPreference = 'Stop'
$name = [string][char]0x006C + [char]0x0074 + [char]0x006F + [char]0x006F + [char]0x006C + ' ' `
        + [char]0x5141 + [char]0x8BB8 + ' '
$targets = @($name + 'ltool', $name + 'ltool-selftest')
$f = New-Object -ComObject HNetCfg.FwPolicy2
foreach ($t in $targets) {
    try {
        $f.Rules.Remove($t)
        Write-Output ('removed: ' + $t)
    } catch {
        Write-Output ('remove failed: ' + $t + ' :: ' + $_.Exception.Message)
    }
}
$left = 0
foreach ($r in $f.Rules) { if ($r.Name -like 'ltool*') { $left++; Write-Output ('still there: ' + $r.Name) } }
Write-Output ('ltool rules left = ' + $left)
