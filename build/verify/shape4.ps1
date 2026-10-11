$ErrorActionPreference = 'Continue'
foreach ($p in @('Software\Classes\*\shell\ltool', 'Software\Classes\Directory\shell\ltool')) {
  $k = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($p)
  if ($null -eq $k) { Write-Output ("不存在: HKCU\" + $p); continue }
  $names = @($k.GetValueNames())
  Write-Output ("KEY HKCU\" + $p)
  Write-Output ('    值名清单 = ' + (($names | ForEach-Object { if ($_ -eq '') { '(默认值!)' } else { $_ } }) -join ', '))
  Write-Output ('    MUIVerb = [' + $k.GetValue('MUIVerb') + ']')
  Write-Output ('    ExtendedSubCommandsKey = [' + $k.GetValue('ExtendedSubCommandsKey') + ']')
  Write-Output ('    残留 SubCommands = [' + $k.GetValue('SubCommands') + ']')
  Write-Output ('    Icon = [' + $k.GetValue('Icon') + ']')
  $sh = $k.OpenSubKey('shell')
  if ($sh) {
    foreach ($n in $sh.GetSubKeyNames()) {
      $c = $sh.OpenSubKey($n)
      $cmd = $sh.OpenSubKey($n + '\command')
      $cmdText = '没有 command'
      if ($cmd) { $cmdText = $cmd.GetValue('') }
      $hasDefault = '否'
      if (@($c.GetValueNames()) -contains '') { $hasDefault = '是(不该有)' }
      Write-Output ('      项 ' + $n + '  MUIVerb=[' + $c.GetValue('MUIVerb') + ']  父级默认值存在=' + $hasDefault)
      Write-Output ('           command=' + $cmdText)
    }
  } else {
    Write-Output '    没有 shell 子键'
  }
}
