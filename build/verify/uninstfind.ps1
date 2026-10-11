$ErrorActionPreference = 'Stop'
$roots = @(
  'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall',
  'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall',
  'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall'
)
function ExeOf([string]$cmd) {
  if (-not $cmd) { return '' }
  $c = $cmd.Trim()
  if ($c.StartsWith('"')) {
    $e = $c.IndexOf('"', 1)
    if ($e -gt 1) { return $c.Substring(1, $e - 1) }
    return $c.Trim('"')
  }
  $i = $c.IndexOf(' .exe', [System.StringComparison]::OrdinalIgnoreCase)
  if ($i -gt 0) { return $c.Substring(0, $i + 4) }
  return ($c -split ' ')[0]
}
$out = New-Object System.Collections.Generic.List[string]
$subs = @('', 'bin', 'Bin', 'sbin', 'app', 'application', 'Application', 'tools', 'uninstall', 'Uninstall')
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
    $exe = ExeOf $us
    $missing = $true
    if ($exe) {
      if (Test-Path -LiteralPath $exe) { $missing = $false }
      elseif ($exe -notmatch '[\\/]') { $missing = $false }   # bare name resolves via system dirs
    }
    if (-not $missing) { continue }
    $loc = [string]$p.InstallLocation
    $hits = @()
    if ($loc -and (Test-Path -LiteralPath $loc -PathType Container)) {
      foreach ($s in $subs) {
        $d = if ($s) { Join-Path $loc $s } else { $loc }
        if (-not (Test-Path -LiteralPath $d -PathType Container)) { continue }
        foreach ($f in Get-ChildItem -LiteralPath $d -Filter *.exe -File -ErrorAction SilentlyContinue) {
          if ($f.BaseName -match '^(unins|uninstall|uninstaller)' -or $f.BaseName -match 'uninstall') {
            $hits += ($f.FullName + '|' + $f.Length)
          }
        }
      }
    }
    $out += ('NAME=' + $name + ' || regExe=' + $exe + ' || loc=' + $loc + ' || found=' + ($hits -join ' , '))
  }
}
$head = 'total broken-command entries = ' + $out.Count
[System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\uninstfind.txt', @($head) + $out, (New-Object System.Text.UTF8Encoding($true)))
'wrote ' + $out.Count + ' rows'
