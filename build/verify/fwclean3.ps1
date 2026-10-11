param([string]$log = "D:\\project\\cpp\\ltool\\build\\verify\\fwclean3.log")
Start-Transcript -Path $log -Force | Out-Null
"pid=$PID IsInRole admin=" + ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
$fw = New-Object -ComObject HNetCfg.FwPolicy2
$names = @()
foreach ($r in $fw.Rules) { if ($r.Name -like "ltool *") { $names += $r.Name } }
"matched=$($names.Count)"
foreach ($n in $names) {
  try { $fw.Rules.Remove($n); "removed: $n" } catch { "FAILED to remove ${n}: $($_.Exception.Message)" }
}
Start-Sleep -Milliseconds 500
$left = @()
foreach ($r in $fw.Rules) { if ($r.Name -like "ltool *") { $left += ($r.Name + " | app=" + $r.ApplicationName) } }
"remaining=$($left.Count)"
$left | ForEach-Object { "left: $_" }
Stop-Transcript | Out-Null
