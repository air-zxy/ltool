$ErrorActionPreference = 'Continue'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$p = 'HKCU\Software\Classes\*\shell\ltool'
Write-Output '--- 方式1：reg.exe ---'
$raw = & cmd.exe /c "reg query `"HKEY_CURRENT_USER\$p`" /s"
Write-Output ("行数=" + @($raw).Count)
$raw | ForEach-Object { Write-Output ('|' + $_) }
Write-Output '--- 方式2：.NET Registry ---'
$k = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey('Software\Classes\*\shell\ltool')
if ($null -eq $k) { Write-Output 'k=null'; exit }
Write-Output ('默认值=' + $k.GetValue('') + ' MultiEnum=' + ($k.GetValue('MultiEnum') -join ',') + ' MuiVer=' + $k.GetValue('MuiVer'))
foreach ($s in $k.GetSubKeyNames()) {
  $sk = $k.OpenSubKey($s)
  $cmd = $sk.OpenSubKey('command')
  Write-Output ('  项=' + $s + '  MUI_Back=' + ($sk.GetValue('MUI_Back') -join '|') + '  command=' + $cmd.GetValue(''))
}
