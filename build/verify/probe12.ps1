$ErrorActionPreference = 'Continue'
$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
$seven = 'C:\Program Files\7-Zip\7z.exe'
$t = 'D:\project\cpp\ltool\build\verify\probe12'
$log = New-Object System.Collections.Generic.List[string]

function TryOpenExclusive($path) {
  try {
    $fs = [IO.File]::Open($path, 'Open', 'ReadWrite', 'None')
    $fs.Close()
    return '没被占用'
  } catch { return '占用中: ' + $_.Exception.Message.Split([char]10)[0] }
}
function TryDelete($path) {
  try { Remove-Item -LiteralPath $path -Force -ErrorAction Stop; return '删掉了' }
  catch { return '删不掉: ' + $_.Exception.Message.Split([char]10)[0] }
}

if (Test-Path -LiteralPath $t) { Remove-Item -LiteralPath $t -Recurse -Force }
New-Item -ItemType Directory -Force -Path $t | Out-Null
# 拿一个真实的大目录当样本（很多文件 + 几百 MB），拷进 scratch，不碰你的东西
$src = 'D:\software\ltool'
Copy-Item -LiteralPath $src "$t\App" -Recurse -Force
$cnt = @(Get-ChildItem -LiteralPath "$t\App" -Recurse -File).Count
$bytes = (Get-ChildItem -LiteralPath "$t\App" -Recurse -File | Measure-Object Length -Sum).Sum
$log.Add(('样本: ' + $cnt + ' 个文件, ' + [math]::Round($bytes/1MB,1) + ' MB'))

foreach ($kind in @('--compress-7z', '--compress-zip')) {
  $out = if ($kind -eq '--compress-7z') { "$t\App.7z" } else { "$t\App.zip" }
  if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Force }
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $p = Start-Process -FilePath $exe -ArgumentList @($kind, "$t\App") -PassThru
  $exited = $p.WaitForExit(600000)
  $sw.Stop()
  $log.Add("$kind 进程退出=$exited 用时=" + [math]::Round($sw.Elapsed.TotalSeconds,1) + 's 包存在=' + (Test-Path -LiteralPath $out))
  # 立刻试独占打开 + 改名 + 删除，并且问一句"谁占着"
  $log.Add('  刚压完 独占打开: ' + (TryOpenExclusive $out))
  $who = & $exe --whohastest $out 2>&1
  $log.Add('  谁占用着它: ' + (($who | Out-String).Trim() -replace '\s+', ' '))
  $ltool = @(Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { $_.Id })
  $log.Add('  此刻 ltool 进程: ' + ($ltool -join ','))
  $log.Add('  立刻删除: ' + $(TryDelete $out))
  Start-Sleep -Seconds 2
  if (Test-Path -LiteralPath $out) { $log.Add('  2 秒后再删: ' + $(TryDelete $out)) }
}
[System.IO.File]::WriteAllLines("$t\_report.txt", $log, (New-Object System.Text.UTF8Encoding($true)))
Write-Output 'done'
