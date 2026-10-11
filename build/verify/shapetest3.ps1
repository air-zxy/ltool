$ErrorActionPreference = 'Continue'
# 完全照抄微软自己那把键的形状（HKLM\...\Directory\shell\UpdateEncryptionSettings）：
#   父键：不设默认值；MUIVerb=名字；ExtendedSubCommandsKey=指向父键自己；子项在 父键\shell\<项>
#   子项：MUIVerb=菜单文字；command 默认值=命令行
$exe = 'D:\software\ltool\ltool.exe'
$items = @(
  @{ k = 'extract';      l = '解压到当前目录';   f = '--extract';      dir = $false },
  @{ k = 'extract-sub';  l = '解压到同名文件夹'; f = '--extract-sub';  dir = $false },
  @{ k = 'compress-zip'; l = '压缩为 .zip';     f = '--compress-zip'; dir = $true },
  @{ k = 'compress-7z';  l = '压缩为 .7z';      f = '--compress-7z';  dir = $true },
  @{ k = 'shred';        l = '粉碎（不可恢复）…'; f = '--shred';        dir = $true },
  @{ k = 'fileinfo';     l = '占用分析';         f = '--fileinfo';     dir = $true },
  @{ k = 'whohas';       l = '谁占用着它';       f = '--whohas';       dir = $true }
)
$RK = [Microsoft.Win32.Registry]::CurrentUser
$RK.DeleteSubKeyTree('Software\Classes\ltoolverbs.file', $false)
$RK.DeleteSubKeyTree('Software\Classes\ltoolverbs.dir', $false)
foreach ($p in @('Software\Classes\*\shell\ltool', 'Software\Classes\Directory\shell\ltool')) { $RK.DeleteSubKeyTree($p, $false) }
foreach ($which in @('file', 'dir')) {
  $rel = if ($which -eq 'file') { 'Software\Classes\*\shell\ltool' } else { 'Software\Classes\Directory\shell\ltool' }
  $pk = $RK.CreateSubKey($rel)
  $pk.SetValue('MUIVerb', 'ltool', [Microsoft.Win32.RegistryValueKind]::String)
  $pk.SetValue('Icon', "$exe,0", [Microsoft.Win32.RegistryValueKind]::String)
  $pk.SetValue('ExtendedSubCommandsKey', $rel, [Microsoft.Win32.RegistryValueKind]::String)
  Write-Output ('父键 ' + $rel + '  值名=[' + (($pk.GetValueNames()) -join ',') + ']  默认值存在=' + (($pk.GetValueNames()) -contains ''))
  $sh = $pk.CreateSubKey('shell')
  $n = 0
  foreach ($it in $items) {
    if ($which -eq 'dir' -and -not $it.dir) { continue }
    $ck = $sh.CreateSubKey($it.k)
    $ck.SetValue('MUIVerb', $it.l, [Microsoft.Win32.RegistryValueKind]::String)
    $cmd = $ck.CreateSubKey('command')
    $cmd.SetValue('', ('"{0}" {1} "%1"' -f $exe, $it.f), [Microsoft.Win32.RegistryValueKind]::String)
    $n++
  }
  Write-Output ('   写了 ' + $n + ' 项')
}
