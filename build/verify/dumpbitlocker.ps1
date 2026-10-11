$ErrorActionPreference = 'Continue'
$raw = & cmd.exe /c 'reg query "HKLM\SOFTWARE\Classes\Directory\shell\UpdateEncryptionSettings" /s'
$raw | ForEach-Object { Write-Output $_ }
Write-Output '--- 用 .NET 再看一遍 ---'
$k = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey('SOFTWARE\Classes\Directory\shell\UpdateEncryptionSettings')
if ($null -eq $k) { Write-Output '父键读不到'; exit }
Write-Output ('父键值名=[' + (($k.GetValueNames()) -join ',') + ']')
foreach ($v in $k.GetValueNames()) {
  Write-Output ('   值 [' + $v + '] = [' + ($k.GetValue($v) -join ';') + '] 类型=' + $k.GetValueKind($v))
}
foreach ($s in $k.GetSubKeyNames()) {
  $sk = $k.OpenSubKey($s)
  Write-Output ('   子键 [' + $s + '] 值名=[' + (($sk.GetValueNames()) -join ',') + ']')
  foreach ($v in $sk.GetValueNames()) {
    Write-Output ('        值 [' + $v + '] = [' + ($sk.GetValue($v) -join ';') + '] 类型=' + $sk.GetValueKind($v))
  }
  foreach ($t in $sk.GetSubKeyNames()) {
    $tk = $sk.OpenSubKey($t)
    Write-Output ('        子子键 [' + $t + '] 值名=[' + (($tk.GetValueNames()) -join ',') + '] 默认=[' + $tk.GetValue('') + ']')
  }
}
