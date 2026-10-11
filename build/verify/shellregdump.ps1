$ErrorActionPreference = 'Continue'
$keys = @('HKCU\Software\Classes\*\shell\ltool', 'HKCU\Software\Classes\Directory\shell\ltool')
foreach ($k in $keys) {
  $raw = & cmd.exe /c "reg query `"HKEY_CURRENT_USER\$k`" /s 2>nul"
  $n = @($raw | Where-Object { $_ -like 'HKEY_*' }).Count
  Write-Output ("$k  子键数=" + $n)
  foreach ($line in $raw) {
    if ($line -match 'REG_SZ' -and $line -match '--') { Write-Output ('   ' + $line.Trim()) }
  }
}
$run = Get-Process ltool -ErrorAction SilentlyContinue
if ($run) { Write-Output ('ltool 正在运行 pid=' + ($run.Id -join ',') + ' path=' + $run.Path) } else { Write-Output 'ltool 没在运行' }
