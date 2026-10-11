$ErrorActionPreference = 'Continue'
foreach ($p in @('HKCU\Software\Classes\*\shell\ltool', 'HKCU\Software\Classes\Directory\shell\ltool')) {
  $k = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($p.Substring(5))
  if ($null -eq $k) { Write-Output ("不存在: " + $p); continue }
  Write-Output ("KEY " + $p)
  Write-Output ('    值名清单 = ' + (($k.GetValueNames() | ForEach-Object { if ($_ -eq '') { '(默认)' } else { $_ } }) -join ', '))
  Write-Output ('    默认值   = [' + $k.GetValue('') + ']')
  Write-Output ('    SubCommands = [' + ($k.GetValue('SubCommands') -join ';') + ']')
  Write-Output ('    MUIVerb  = [' + $k.GetValue('MUIVerb') + ']')
  Write-Output ('    ExtendedSubCommandsKey = [' + $k.GetValue('ExtendedSubCommandsKey') + ']')
  $sh = $k.OpenSubKey('shell')
  if ($sh) {
    Write-Output ('    shell\ 子项 = ' + (($sh.GetSubKeyNames()) -join ', '))
    foreach ($n in $sh.GetSubKeyNames()) {
      $c = $sh.OpenSubKey($n + '\command')
      Write-Output ('      ' + $n + ' 标签=[' + $sh.OpenSubKey($n).GetValue('MUIVerb') + '] 默认值存在=' + (($sh.OpenSubKey($n)).GetValueNames() -contains '') + ' -> ' + $(if ($c) { $c.GetValue('') } else { '没有 command' })
    }
  }
}
