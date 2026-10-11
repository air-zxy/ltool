$ErrorActionPreference = 'Stop'
$inner = 'D:\project\cpp\ltool\build\verify\fwmigrate_inner.ps1'
$log = 'D:\project\cpp\ltool\build\verify\fwmigrate.log'
$body = @'
Start-Transcript -Path 'LOGPATH' -Force | Out-Null
$f = New-Object -ComObject HNetCfg.FwPolicy2
$test = 'BFF'
try { $f.Rules.Remove($test); "removed test rule: $test" } catch { "no test rule to remove: $($_.Exception.Message)" }
$ren = 0
foreach ($r in @($f.Rules)) {
  $n = [string]$r.Name
  if ($n -like 'ltool *') {
    $new = $n -replace '^ltool\s+', ''
    $new = $new -replace '\s*(版本|version)\s*[0-9][0-9a-zA-Z._\-]*\s*$', ''
    $new = $new.Trim()
    if ($new) {
      $app = ''
      try { $app = [string]$r.ApplicationName } catch { }
      $r.Name = $new
      $r.Description = 'ltool 添加 · 允许入站 · ' + $app
      $ren += 1
      "renamed: [$n] -> [$new]"
    }
  }
}
"renamed total = $ren"
$left = 0
foreach ($r in @($f.Rules)) { if ($r.Name -like 'ltool *') { $left += 1; "still prefixed: " + $r.Name } }
"still prefixed = $left"
foreach ($r in @($f.Rules)) {
  if ($r.Description -like 'ltool*') {
    "OURS: name=" + $r.Name + " | dir=" + $r.Direction + " act=" + $r.Action + " on=" + $r.Enabled
  }
}
Stop-Transcript | Out-Null
'@
$body = $body.Replace('LOGPATH', $log)
[System.IO.File]::WriteAllText($inner, $body, (New-Object System.Text.UTF8Encoding($true)))
$p = Start-Process powershell -Verb RunAs -WindowStyle Hidden -PassThru -ArgumentList @(
  '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $inner)
$null = $p.WaitForExit(90000)
"elevated exit code = " + $p.ExitCode
