$ErrorActionPreference = 'Stop'
$roots = @(
  @{ tag = 'HKLM64'; path = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall' },
  @{ tag = 'HKLM32'; path = 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall' },
  @{ tag = 'HKCU';   path = 'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall' }
)
$out = New-Object System.Collections.Generic.List[string]
foreach ($r in $roots) {
  if (-not (Test-Path $r.path)) { continue }
  foreach ($k in Get-ChildItem $r.path -ErrorAction SilentlyContinue) {
    $p = Get-ItemProperty $k.PSPath
    $n = [string]$p.DisplayName
    if (-not $n) { continue }
    if ($n -notmatch 'BFF|bff') { continue }
    $out += ('[' + $r.tag + '] key=' + $k.PSChildName)
    $out += ('    DisplayName   = ' + $n)
    $out += ('    DisplayVersion= ' + [string]$p.DisplayVersion)
    $out += ('    Publisher     = ' + [string]$p.Publisher)
    $out += ('    InstallDate   = ' + [string]$p.InstallDate)
    $out += ('    InstallLocation = ' + [string]$p.InstallLocation)
    $out += ('    DisplayIcon   = ' + [string]$p.DisplayIcon)
    $out += ('    UninstallString = ' + [string]$p.UninstallString)
    $out += ('    QuietUninstallString = ' + [string]$p.QuietUninstallString)
    $out += ('    SystemComponent=' + [string]$p.SystemComponent + ' Hidden=' + [string]$p.Hidden +
             ' WindowsInstaller=' + [string]$p.WindowsInstaller)
    $out += ('    EstimatedSize = ' + [string]$p.EstimatedSize)
  }
}
[System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\bff.txt', $out,
  (New-Object System.Text.UTF8Encoding($true)))
'rows=' + ($out.Count / 11)
