$ErrorActionPreference = 'Continue'
$sh = New-Object -ComObject Shell.Application
$cases = @(
  @{ root = 'D:\project\cpp\ltool\build\verify\expmenu'; leaf = 'afile.txt' },
  @{ root = 'D:\project\cpp\ltool\build\verify\expmenu'; leaf = 'afolder' }
)
$out = New-Object System.Collections.Generic.List[string]
foreach ($c in $cases) {
  $item = $sh.Namespace($c.root).ParseName($c.leaf)
  if ($null -eq $item) { $out.Add('ParseName 失败 ' + $c.leaf); continue }
  $out.Add('=== ' + $c.root + '\' + $c.leaf)
  foreach ($v in $item.Verbs()) {
    $n = $v.Name
    if ($n -like '*ltool*' -or $n -like '*压缩*' -or $n -like '*解压*' -or $n -like '*粉碎*' -or $n -like '*占用*') {
      $out.Add('   VERB [' + $n + ']')
    }
  }
}
[System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\verbs2.txt', $out, (New-Object System.Text.UTF8Encoding($true)))
Write-Output 'written'
