$ErrorActionPreference = 'Continue'
$cn = [string][char]0x5185 + [char]0x5B58 + [char]0x4FEE + [char]0x6539 + [char]0x5668
$sh = New-Object -ComObject Shell.Application
$cases = @(
  @{ root = [Environment]::GetFolderPath('Desktop'); leaf = $cn },
  @{ root = 'D:\project\cpp\ltool'; leaf = 'vcpkg.json' }
)
$out = New-Object System.Collections.Generic.List[string]
foreach ($c in $cases) {
  $path = Join-Path $c.root $c.leaf
  $out.Add('=== ' + $path + '  exists=' + (Test-Path -LiteralPath $path))
  $item = $sh.Namespace($c.root).ParseName($c.leaf)
  if ($null -eq $item) { $out.Add('  ParseName 失败'); continue }
  $i = 0
  foreach ($v in $item.Verbs()) {
    $i++
    $out.Add(('  [{0,2}] {1}' -f $i, $v.Name))
  }
}
[System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\verblist.txt', $out, (New-Object System.Text.UTF8Encoding($true)))
Write-Output 'written'
