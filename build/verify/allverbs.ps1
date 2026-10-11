$ErrorActionPreference = 'Continue'
$sh = New-Object -ComObject Shell.Application
foreach ($p in @(@('D:\project\cpp\ltool\build\verify\menutest', 'sample.txt'),
                 @('D:\project\cpp\ltool\build\verify\menutest', ''))) {
  $ns = $sh.Namespace($p[0])
  if ($p[1] -eq '') { $item = $ns.Self } else { $item = $ns.ParseName($p[1]) }
  Write-Output ('=== ' + $p[0] + '\' + $p[1])
  $i = 0
  foreach ($v in $item.Verbs()) {
    $i++
    Write-Output ('  [{0,2}] [{1}]' -f $i, $v.Name)
  }
}
