Start-Transcript -Path 'D:\project\cpp\ltool\build\verify\fwmigrate2.log' -Force | Out-Null
$f = New-Object -ComObject HNetCfg.FwPolicy2
$ren = 0
foreach ($r in @($f.Rules)) {
  $d = [string]$r.Description
  if ($d -notlike 'ltool*') { continue }
  $n = [string]$r.Name
  $new = $n -replace '^(ltool\s+)?(允许\s+)?', ''
  $new = $new -replace '\s*(版本|version)\s*[0-9][0-9a-zA-Z._\-]*\s*$', ''
  $new = $new.Trim()
  if ($new -and $new -ne $n) {
    $r.Name = $new
    $ren += 1
    "renamed [$n] -> [$new]"
  }
}
"renamed total = $ren"
foreach ($r in @($f.Rules)) {
  if ([string]$r.Description -like 'ltool*') {
    "OURS: name=" + $r.Name + " | dir=" + $r.Direction + " act=" + $r.Action + " on=" + $r.Enabled + " | app=" + [string]$r.ApplicationName
  }
}
Stop-Transcript | Out-Null
