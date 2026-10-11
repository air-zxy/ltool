$ErrorActionPreference = 'Continue'
# 把 HKCU 下我们写过的两个右键根，逐个叶子键检查：有没有 command、command 指向哪里
$roots = @{
  'file'      = 'Software\Classes\*\shell\ltool';
  'dir'       = 'Software\Classes\Directory\shell\ltool';
  'dirlower'  = 'Software\Classes\directory\shell\ltool';
  'folder'    = 'Software\Classes\Folder\shell\ltool'
}
foreach ($k in $roots.Keys) {
  $rk = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($roots[$k])
  if ($null -eq $rk) { Write-Output ("[{0}] {1} 不存在" -f $k, $roots[$k]); continue }
  Write-Output ("[{0}] {1}  默认值=[{2}]  Icon=[{3}]" -f $k, $roots[$k], $rk.GetValue(''), $rk.GetValue('Icon'))
  foreach ($s in $rk.GetSubKeyNames()) {
    $sk = $rk.OpenSubKey($s)
    $cmdKey = $sk.OpenSubKey('command')
    $cmd = if ($cmdKey) { $cmdKey.GetValue('') } else { $null }
    $flag = if ($null -eq $cmdKey) { '没有 command 子键 <<< 点了就报"没有关联应用"' } elseif ([string]::IsNullOrEmpty($cmd)) { 'command 是空的 <<< 同上' } else { 'ok' }
    Write-Output ("    项={0,-14} 标签=[{1}]  {2}" -f $s, $sk.GetValue(''), $flag)
    if ($cmd) { Write-Output ("                     命令=" + $cmd) }
    $vals = @($sk.GetValueNames()) + '|' + @($sk.GetSubKeyNames())
    Write-Output ("                     值名=" + ($sk.GetValueNames() -join ',') + ' 子键=' + ($sk.GetSubKeyNames() -join ','))
  }
}
$exe = 'D:\software\ltool\ltool.exe'
Write-Output ('ltool.exe 存在=' + (Test-Path -LiteralPath $exe))
