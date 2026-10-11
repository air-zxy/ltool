$ErrorActionPreference = 'Stop'
$roots = @(
  'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
  'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*',
  'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*'
)
$all = @()
foreach ($r in $roots) { $all += (Get-ItemProperty -Path $r -ErrorAction SilentlyContinue) }
$named = @($all | Where-Object { $_.PSObject.Properties.Name -contains 'DisplayName' -and $_.DisplayName })
$sys = @($named | Where-Object { $_.SystemComponent -eq 1 })
$hid = @($named | Where-Object { $_.Hidden -eq 1 })
$nourl = @($named | Where-Object { -not $_.UninstallString -and -not $_.QuietUninstallString })
Write-Output ('total keys      = ' + $all.Count)
Write-Output ('with DisplayName= ' + $named.Count)
Write-Output ('SystemComponent = ' + $sys.Count)
Write-Output ('Hidden          = ' + $hid.Count)
Write-Output ('no uninstall cmd= ' + $nourl.Count)
Write-Output ('visible (non-system, non-hidden) = ' + @($named | Where-Object { $_.SystemComponent -ne 1 -and $_.Hidden -ne 1 }).Count)
