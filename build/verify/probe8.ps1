$ErrorActionPreference = 'Continue'
$exe = 'D:\software\ltool\ltool.exe'
$t = 'D:\project\cpp\ltool\build\verify\probe8'
$cn = [string][char]0x8BF4 + [char]0x660E
$seven = 'C:\Program Files\7-Zip\7z.exe'
$log = New-Object System.Collections.Generic.List[string]

if (Test-Path $t) { Remove-Item $t -Recurse -Force }
New-Item -ItemType Directory -Force -Path "$t\src" | Out-Null
Set-Content -LiteralPath "$t\src\$cn.txt" -Value ('A' * 1200) -Encoding UTF8
Set-Content -LiteralPath "$t\src\ascii.txt" -Value ('B' * 900) -Encoding UTF8
& $seven a -tgzip "$t\ascii.gz" "$t\src\ascii.txt" | Out-Null
& $seven a -tgzip "$t\$cn.txt.gz" "$t\src\$cn.txt" | Out-Null

# 全部用 --archivetest（无界面）先看后端本身对单文件 gz 的表现
foreach ($f in @('ascii.gz', "$cn.txt.gz")) {
  $o = "$t\out_" + ($f -replace '[^a-zA-Z]', 'x')
  $null = Start-Process -FilePath $exe -ArgumentList @('--archivetest', 'extract', "$t\$f", $o) -Wait -NoNewWindow -RedirectStandardOutput "$t\cli.txt"
  $txt = Get-Content -LiteralPath "$t\cli.txt" -Raw -Encoding UTF8
  $log.Add('cli ' + $f + ' -> ' + $txt.Trim())
  $log.Add('  out tree: ' + ((@(Get-ChildItem -LiteralPath $o -Recurse -File -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName.Substring($t.Length + 1) }) -join ',')))
}

# 再走界面：分别对 ascii.gz 和中文名 gz 按 --extract-sub
foreach ($f in @('ascii.gz', "$cn.txt.gz")) {
  $tag = $f
  $p = Start-Process -FilePath $exe -ArgumentList @('--extract-sub', "$t\$f") -PassThru
  Start-Sleep -Seconds 2
  $alive = -not $p.WaitForExit(200)
  $log.Add('gui ' + $tag + ' pid=' + $p.Id + ' alive(after 2s)=' + $alive)
  if (-not $alive) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
}
Start-Sleep -Milliseconds 500
$log.Add('tree: ' + ((@(Get-ChildItem -LiteralPath $t -Recurse | ForEach-Object { $_.FullName.Substring($t.Length + 1) }) -join ' ; ')))
[System.IO.File]::WriteAllLines("$t\_report.txt", $log, (New-Object System.Text.UTF8Encoding($true)))
Write-Output 'done'
