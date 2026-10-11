$ErrorActionPreference = 'Continue'
$log = New-Object System.Collections.Generic.List[string]
foreach ($hive in @('HKLM', 'HKCU')) {
  $cs = "$hive`:\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\CommandStore\shell"
  $exists = Test-Path -LiteralPath $cs
  $log.Add("=== $cs 存在=$exists")
  if (-not $exists) { continue }
  $kids = @(Get-ChildItem -LiteralPath $cs -ErrorAction SilentlyContinue)
  $log.Add("   条目数=" + $kids.Count + '  前 12 个名字: ' + (($kids | Select-Object -First 12 | ForEach-Object { $_.PSChildName }) -join ', '))
  foreach ($k in ($kids | Select-Object -First 4)) {
    $item = Get-Item -LiteralPath $k.PSPath
    $log.Add('   项 ' + $k.PSChildName + ' 值名=[' + (($item.GetValueNames()) -join ',') + ']')
    foreach ($v in $item.GetValueNames()) {
      $log.Add(('      值 [{0}] = [{1}]' -f $v, $item.GetValue($v)))
    }
    $cmd = Join-Path $k.PSPath 'command'
    if (Test-Path -LiteralPath $cmd) {
      $log.Add('      command\默认值 = [' + (Get-Item -LiteralPath $cmd).GetValue('') + ']')
    } else {
      $log.Add('      没有 command 子键')
    }
    $sh = Join-Path $k.PSPath 'shell'
    if (Test-Path -LiteralPath $sh) {
      $log.Add('      还有更深的 shell 层: ' + ((Get-ChildItem -LiteralPath $sh | ForEach-Object { $_.PSChildName }) -join ','))
    }
  }
}
# 顺便看看有没有现成的级联菜单用了 SubCommands（父键在 HKCU/HKLM 的 shell 下）
foreach ($p in @('HKLM:\SOFTWARE\Classes\Directory\shell', 'HKLM:\SOFTWARE\Classes\*\shell',
                 'HKCU:\Software\Classes\*\shell', 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\VerbSequences')) {
  if (-not (Test-Path -LiteralPath $p)) { continue }
  foreach ($k in @(Get-ChildItem -LiteralPath $p -ErrorAction SilentlyContinue)) {
    $item = Get-Item -LiteralPath $k.PSPath
    if ($item.GetValueNames() -contains 'SubCommands') {
      $log.Add(('现成级联: {0}  SubCommands=[{1}]  MUIVerb=[{2}]  默认值=[{3}]' -f $k.PSPath, $item.GetValue('SubCommands'), $item.GetValue('MUIVerb'), $item.GetValue('')))
    }
  }
}
[System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\cmdstore.txt', $log, (New-Object System.Text.UTF8Encoding($true)))
Write-Output 'written'
