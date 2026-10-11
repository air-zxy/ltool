$ErrorActionPreference = 'Continue'
$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
$t = 'D:\project\cpp\ltool\build\verify\probe13'
$log = New-Object System.Collections.Generic.List[string]

function TryOpenExclusive($path) {
  try { $fs = [IO.File]::Open($path, 'Open', 'ReadWrite', 'None'); $fs.Close(); return '没被占用' }
  catch { return '占用中' }
}
if (Test-Path -LiteralPath $t) { Remove-Item -LiteralPath $t -Recurse -Force }
New-Item -ItemType Directory -Force -Path "$t\App" | Out-Null
$rand = New-Object byte[] 1048576
$rng = New-Object Random
for ($i = 0; $i -lt 60; $i++) {
  $rng.NextBytes($rand)
  [IO.File]::WriteAllBytes("$t\App\rand$i.bin", $rand)
}
$log.Add('样本: 60 个随机文件 = ' + [math]::Round((Get-ChildItem "$t\App" | Measure-Object Length -Sum).Sum / 1MB, 1) + ' MB')

$sw = [Diagnostics.Stopwatch]::StartNew()
$p = Start-Process -FilePath $exe -ArgumentList @('--compress-7z', "$t\App") -PassThru
while (-not $p.WaitForExit(0)) {
  Start-Sleep -Milliseconds 1500
  $names = @(Get-ChildItem -LiteralPath $t -Filter 'App.7z*' | ForEach-Object { $_.Name + '=' + $_.Length })
  $log.Add(('t={0,5:N1}s 桌面上: {1}' -f $sw.Elapsed.TotalSeconds, ($names -join ', ')))
  if ($sw.Elapsed.TotalSeconds -gt 180) { break }
}
$sw.Stop()
$log.Add('进程退出，总用时 ' + [math]::Round($sw.Elapsed.TotalSeconds, 1) + 's 退出码=' + $p.ExitCode)
$log.Add('结束后文件: ' + ((@(Get-ChildItem -LiteralPath $t -Filter 'App.7z*' | ForEach-Object { $_.Name + '=' + $_.Length }) -join ', ')))
$log.Add('成品能不能独占打开: ' + (TryOpenExclusive "$t\App.7z"))
$log.Add('成品能不能改名: ' + $(try { Rename-Item -LiteralPath "$t\App.7z" "$t\App.7z.r"; Rename-Item -LiteralPath "$t\App.7z.r" "$t\App.7z"; '可以' } catch { '不行' }))
# 7z 验一下成品
$v = & 'C:\Program Files\7-Zip\7z.exe' t "$t\App.7z" 2>&1 | Out-String
$log.Add('7z t: ' + ((($v -split "`n" | Where-Object { $_ -match 'Everything is Ok|Error' }) -join ' ').Trim()))
# zip 也走一遍
$null = Start-Process -FilePath $exe -ArgumentList @('--compress-zip', "$t\App") -Wait -PassThru
$log.Add('zip 结束后: ' + ((@(Get-ChildItem -LiteralPath $t -Filter 'App.zip*' | ForEach-Object { $_.Name + '=' + $_.Length }) -join ', ')))
[System.IO.File]::WriteAllLines("$t\_report.txt", $log, (New-Object System.Text.UTF8Encoding($true)))
Write-Output 'done'
