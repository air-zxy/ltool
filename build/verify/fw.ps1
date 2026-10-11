$ErrorActionPreference = 'Stop'
$id = [Security.Principal.WindowsIdentity]::GetCurrent()
$p = New-Object Security.Principal.WindowsPrincipal($id)
Write-Output ('elevated=' + $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator))
try {
    $f = New-Object -ComObject HNetCfg.FwPolicy2
    Write-Output ('rules readable=' + $f.Rules.Count)
} catch {
    Write-Output ('read failed: ' + $_.Exception.Message)
}
