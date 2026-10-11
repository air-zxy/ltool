$ErrorActionPreference = 'Continue'
# 用 Shell.Application 的 FolderItem.Verbs()/InvokeVerb() 复现"资源管理器右键点某一项"，
# 走的就是 Explorer 自己那条 ShellExecuteEx 路径，不需要合成鼠标点击。
$cn = [string][char]0x5185 + [char]0x5B58 + [char]0x4FEE + [char]0x6539 + [char]0x5668   # 内存修改器
$targets = @(
  @{ what = 'Desktop 上的文件夹'; root = 'C:\Users\loukas\Desktop'; leaf = $cn },
  @{ what = 'D 盘普通文件夹';     root = 'D:\project\cpp\ltool';    leaf = 'build' },
  @{ what = 'D 盘普通文件';       root = 'D:\project\cpp\ltool';    leaf = 'vcpkg.json' }
)
$sh = New-Object -ComObject Shell.Application
foreach ($t in $targets) {
  Write-Output ('=== ' + $t.what + ' : ' + $t.root + '\' + $t.leaf)
  if (-not (Test-Path -LiteralPath (Join-Path $t.root $t.leaf))) { Write-Output '    目标不存在，跳过'; continue }
  $folder = $sh.Namespace($t.root)
  $item = $folder.ParseName($t.leaf)
  if ($null -eq $item) { Write-Output '    ParseName 失败'; continue }
  $vs = @($item.Verbs())
  $mine = @($vs | Where-Object { $_.Name -like '*ltool*' })
  Write-Output ('    verbs 总数=' + $vs.Count + '  含 ltool 的=' + $mine.Count)
  foreach ($v in $mine) { Write-Output ('    VERB [' + $v.Name + ']') }
}
