$ErrorActionPreference = 'Continue'
$log = New-Object System.Collections.Generic.List[string]
$targets = @(
  'HKLM\SOFTWARE\Classes\Directory',
  'HKLM\SOFTWARE\Classes\AllFilesystemObjects',
  'HKLM\SOFTWARE\Classes\Drive',
  'HKLM\SOFTWARE\Classes\Folder'
)
foreach ($t in $targets) {
  $raw = & cmd.exe /c "reg query `"$t`" /f ExtendedSubCommandsKey /s /k 2>nul"
  $hits = @($raw | Where-Object { $_ -like 'HKEY_*' })
  $log.Add("=== $t  命中=" + $hits.Count)
  foreach ($h in ($hits | Select-Object -First 4)) { $log.Add('   ' + $h) }
  $raw2 = & cmd.exe /c "reg query `"$t`" /f ExtendedSubCommandsKey /s 2>nul"
  foreach ($line in $raw2) {
    if ($line -match 'REG_SZ') { $log.Add('   值: ' + $line.Trim()) }
  }
}
[System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\esck.txt', $log, (New-Object System.Text.UTF8Encoding($true)))
Write-Output 'written'
