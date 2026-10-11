$ErrorActionPreference = 'Continue'
# 手改注册表验证假设（用 .NET API，因为路径里有 * ，PowerShell 的 HKCU: 提供程序会当通配符）：
#   父键默认值必须"未设置"，名字放 MUIVerb，ExtendedSubCommandsKey 指向锚点，
#   子项放 锚点\shell\<项>\command —— 对照系统自己的 Directory\shell\UpdateEncryptionSettings\Shell\Decrypt
$exe = 'D:\software\ltool\ltool.exe'
$items = @(
  @{ k = 'extract';      l = '解压到当前目录';    f = '--extract';      dir = $false },
  @{ k = 'extract-sub';  l = '解压到同名文件夹';  f = '--extract-sub';  dir = $false },
  @{ k = 'compress-zip'; l = '压缩为 .zip';      f = '--compress-zip'; dir = $true },
  @{ k = 'compress-7z';  l = '压缩为 .7z';       f = '--compress-7z';  dir = $true },
  @{ k = 'shred';        l = '粉碎（不可恢复）…';  f = '--shred';        dir = $true },
  @{ k = 'fileinfo';     l = '占用分析';          f = '--fileinfo';     dir = $true },
  @{ k = 'whohas';       l = '谁占用着它';        f = '--whohas';       dir = $true }
)
function Put($k, $name, $value) {
  if ($null -eq $name) { $k.SetValue('', $value, [Microsoft.Win32.RegistryValueKind]::String) }
  else { $k.SetValue($name, $value, [Microsoft.Win32.RegistryValueKind]::String) }
}
$RK = [Microsoft.Win32.Registry]::CurrentUser
foreach ($which in @('file', 'dir')) {
  $anchorRel = "Software\Classes\ltoolverbs.$which"
  $parentRel = if ($which -eq 'file') { 'Software\Classes\*\shell\ltool' } else { 'Software\Classes\Directory\shell\ltool' }
  $RK.DeleteSubKeyTree($anchorRel, $false)
  $RK.DeleteSubKeyTree($parentRel, $false)
  $pk = $RK.CreateSubKey($parentRel)
  Put $pk 'MUIVerb' 'ltool'
  Put $pk 'Icon' "$exe,0"
  Put $pk 'ExtendedSubCommandsKey' "ltoolverbs.$which"
  Write-Output ('父键 ' + $parentRel + ' 值名=[' + (($pk.GetValueNames()) -join ',') + '] 默认值存在=' + (($pk.GetValueNames()) -contains ''))
  $ak = $RK.CreateSubKey($anchorRel + '\shell')
  $n = 0
  foreach ($it in $items) {
    if ($which -eq 'dir' -and -not $it.dir) { continue }
    $ck = $ak.CreateSubKey($it.k)
    Put $ck 'MUIVerb' $it.l
    $cmdk = $ck.CreateSubKey('command')
    $cmdk.SetValue('', ('"{0}" {1} "%1"' -f $exe, $it.f), [Microsoft.Win32.RegistryValueKind]::ExpandString)
    $n++
  }
  $ak.Close()
  Write-Output ('   锚点 ' + $anchorRel + ' 写了 ' + $n + ' 项')
}
Write-Output '--- 微软自己的写法长这样：---'
$raw = & cmd.exe /c 'reg query "HKLM\SOFTWARE\Classes\Directory" /f ExtendedSubCommandsKey /s 2>nul'
$raw | Where-Object { $_ -like 'HKEY_*' -or $_ -match 'REG_SZ' } | Select-Object -First 6 | ForEach-Object { Write-Output ('   ' + $_.Trim()) }
