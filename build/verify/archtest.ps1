$ErrorActionPreference = 'Continue'
$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
$work = 'D:\project\cpp\ltool\build\verify\artest'
$report = 'D:\project\cpp\ltool\build\verify\artest_report.txt'
$out = 'D:\project\cpp\ltool\build\verify\artest.out'
$log = New-Object System.Collections.Generic.List[string]

if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory -Force -Path "$work\src\子目录" | Out-Null
Set-Content -Path "$work\src\说明.txt" -Value ('A' * 5000) -Encoding UTF8
Set-Content -Path "$work\src\plain.bin" -Value ([byte[]](1..255)) -Encoding Byte
Set-Content -Path "$work\src\子目录\嵌套.txt" -Value ('B' * 12000) -Encoding UTF8

$seven = 'C:\Program Files\7-Zip\7z.exe'
& $seven a -tzip "$work\pack.zip" "$work\src\*" | Out-Null
& $seven a -t7z "$work\pack.7z" "$work\src\*" | Out-Null
& $seven a -ttar "$work\pack.tar" "$work\src\*" | Out-Null
$python = (Get-Command python -ErrorAction SilentlyContinue).Source
& $python "$work\..\gbkzip.py" "$work\gbk.zip" | Out-Null
& $seven a -tgzip "$work\pack.tar.gz" "$work\pack.tar" | Out-Null
& $seven a -tgzip "$work\one.gz" "$work\src\plain.bin" | Out-Null
Set-Content -Path "$work\notarchive.txt" -Value ('just text, not an archive') -Encoding ASCII
# 进度/取消那两条线要有东西可切：8 × 2MB，fsutil 瞬间建出来（全零，压得飞快但回调照样逐文件走）
New-Item -ItemType Directory -Force -Path "$work\big" | Out-Null
1..8 | ForEach-Object { & fsutil file createnew "$work\big\f$_.bin" 2097152 | Out-Null }
$log.Add('fixtures: ' + (@(Get-ChildItem $work -Filter *.pack* ) | ForEach-Object { $_.Name }) -join ', ')
$log.Add('fixture list: ' + ((Get-ChildItem "$work\src" -Recurse -File | ForEach-Object { $_.Name }) -join ', '))

function Run($args_, $tag) {
  $null = Start-Process -FilePath $script:exe -ArgumentList $args_ -Wait -NoNewWindow -RedirectStandardOutput $script:out
  $txt = Get-Content $script:out -Raw -Encoding UTF8
  $script:log.Add($tag + ' -> ' + $txt.Trim())
}

Run @('--archivetest', 'extract', "$work\pack.zip", "$work\out_zip") 'zip 解压'
Run @('--archivetest', 'extract', "$work\pack.7z", "$work\out_7z") '7z 解压'
Run @('--archivetest', 'extract', "$work\pack.tar.gz", "$work\out_tgz") 'tar.gz 解压'
Run @('--archivetest', 'make', "$work\src", "$work\mine.zip") '压缩成 zip'
Run @('--archivetest', 'make', "$work\src", "$work\mine.7z", '7z') '压缩成 7z'
Run @('--archivetest', 'extract', "$work\mine.zip", "$work\out_mine") '自己产的 zip 再解回来'
Run @('--archivetest', 'extract', "$work\gbk.zip", "$work\out_gbk") 'GBK 名字的老 zip 解压'
Run @('--archivetest', 'extract', "$work\mine.7z", "$work\out_mine7z") '自己产的 7z 再解回来'
Run @('--archivetest', 'extract', "$work\one.gz", "$work\out_gz") '单文件 gz 解压'
Run @('--archivetest', 'probe', "$work\notarchive.txt") '探测：纯文本（应 looksLikeArchive=0）'
Run @('--archivetest', 'probe', "$work\pack.zip") '探测：zip（应 1）'
Run @('--archivetest', 'probe', "$work\one.gz") '探测：单文件 gz（应 1）'
Run @('--archivetest', 'extract', "$work\notarchive.txt", "$work\out_bad") '硬解纯文本（右键走不到这一步）'
# 进度与取消：7z 后端换了 bit7z，这两条只有界面在用的线必须无界面重验一遍。
# 全程不取消那档看分母/单调/落盘，第 3 次按取消那档看 cancelled 和半成品有没有清干净。
Run @('--archivetest', 'progress', "$work\big", "$work\prog_full.7z", '7z', '999999') '7z 进度全程（看 total/单调/成品在）'
Run @('--archivetest', 'progress', "$work\big", "$work\prog_cut.7z", '7z', '3') '7z 第 3 次回调按取消'
Run @('--archivetest', 'progress', "$work\big", "$work\prog_full.zip", 'zip', '999999') 'zip 进度全程（对照，后端没换）'
foreach ($d in @('out_zip', 'out_7z', 'out_tgz', 'out_mine', 'out_mine7z', 'out_gbk', 'out_gz')) {
  $files = @(Get-ChildItem "$work\$d" -Recurse -File -ErrorAction SilentlyContinue)
  $log.Add($d + ': ' + $files.Count + ' 个文件, 合计 ' + (($files | Measure-Object Length -Sum).Sum) + ' 字节, 名字=' + (($files | ForEach-Object { $_.Name }) -join ','))
}
$verify = & $seven t "$work\mine.zip" 2>&1 | Out-String
$log.Add('7z -t mine.zip: ' + ((($verify -split "`n" | Where-Object { $_ -match 'Everything is Ok|Error' }) -join ' ').Trim()))
$verify2 = & $seven t "$work\mine.7z" 2>&1 | Out-String
$log.Add('7z -t mine.7z: ' + ((($verify2 -split "`n" | Where-Object { $_ -match 'Everything is Ok|Error' }) -join ' ').Trim()))
[System.IO.File]::WriteAllLines($report, $log, (New-Object System.Text.UTF8Encoding($true)))
'done'
