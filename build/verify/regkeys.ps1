$ErrorActionPreference = 'Continue'
$out = New-Object System.Collections.Generic.List[string]
function Dump($path) {
  if (-not (Test-Path -LiteralPath $path)) { $out.Add("(键不存在) $path"); return }
  $k = Get-Item -LiteralPath $path
  $out.Add("KEY $path")
  foreach ($v in $k.GetValueNames()) {
    $val = if ($v -eq '') { $k.GetValue('') } else { ($k.GetValue($v) -join ' | ') }
    $out.Add(('   值名=[{0}] 值=[{1}]' -f $v, $val))
  }
}
Dump 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam'
Dump 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Steam'
Dump 'Registry::HKEY_LOCAL_MACHINE\Software\Classes\.bat\shell\open\command'
Dump 'Registry::HKEY_LOCAL_MACHINE\Software\Classes\.exe\shell\open\command'
Dump 'Registry::HKEY_LOCAL_MACHINE\Software\Classes\.blg\shell\open\command'
Dump 'Registry::HKEY_LOCAL_MACHINE\Software\Classes\batfile\shell\open\command'
Dump 'Registry::HKEY_LOCAL_MACHINE\Software\Classes\exefile\shell\open\command'
Dump 'Registry::HKEY_LOCAL_MACHINE\Software\Classes\perfmonfile\shell\open\command'
Dump 'Registry::HKEY_LOCAL_MACHINE\Software\Classes\.msstyles\shell\open\command'
[System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\regkeys.txt', $out, (New-Object System.Text.UTF8Encoding($true)))
Write-Output 'written'
