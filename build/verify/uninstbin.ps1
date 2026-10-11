$ErrorActionPreference = 'Stop'
$roots = @(
  'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall',
  'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall',
  'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall'
)
$out = New-Object System.Collections.Generic.List[string]
$inBin = 0
$total = 0
foreach ($r in $roots) {
  if (-not (Test-Path $r)) { continue }
  foreach ($k in Get-ChildItem $r -ErrorAction SilentlyContinue) {
    $p = Get-ItemProperty $k.PSPath
    $name = [string]$p.DisplayName
    if (-not $name) { continue }
    if ($p.SystemComponent -eq 1) { continue }
    if ($p.PSObject.Properties['Hidden'] -and $p.Hidden -ne 0) { continue }
    $us = [string]$p.UninstallString
    if (-not $us) { $us = [string]$p.QuietUninstallString }
    $total += 1
    if ($us -match '\\bin\\') {
      $inBin += 1
      $out += ('BIN  ' + $name + '  ->  ' + $us)
    }
  }
}
[System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\uninstbin.txt',
  @("entries=$total uninstaller-under-bin=$inBin") + $out, (New-Object System.Text.UTF8Encoding($true)))
'entries=' + $total + ' inBin=' + $inBin
