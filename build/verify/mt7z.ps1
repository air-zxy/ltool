$ErrorActionPreference = 'Continue'
# 7z 压缩 A/B：我们（bit7z/7zip.dll 多线程）vs 7-Zip 命令行，同一份真实目录。
# 三次各跑一遍，取中位数；单次数字不作数（上一轮就是这么翻车的）。
$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
$seven = 'C:\Program Files\7-Zip\7z.exe'
$src = 'C:\Users\loukas\Desktop\WinPlayerD1018'
$work = 'D:\project\cpp\ltool\build\verify\mt7z'
$out = 'D:\project\cpp\ltool\build\verify\mt7z.out'
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory -Force -Path $work | Out-Null
$srcBytes = (Get-ChildItem $src -Recurse -File | Measure-Object Length -Sum).Sum
'夹具: ' + (Get-ChildItem $src -Recurse -File).Count + ' 个文件, ' + [math]::Round($srcBytes / 1MB, 1) + ' MB'

function Timed($tag, $block) {
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $null = & $block
  $sw.Stop()
  [pscustomobject]@{ 组 = $tag; ms = $sw.ElapsedMilliseconds }
}

$rounds = 1..3
$ours = foreach ($i in $rounds) {
  $p = "$work\our$i.7z"
  Timed "我们 $i" { & $exe --archivetest make $src $p 7z 2>&1 | Out-Default }
}
$zip = foreach ($i in $rounds) {
  $p = "$work\ref$i.7z"
  Timed "7z $i" { & $seven a -bso0 -bsp0 "-o$work" "-t7z" '-mx=5' $p "$src\*" | Out-Null }
}
$mt1 = foreach ($i in $rounds) {
  $p = "$work\one$i.7z"
  Timed "7z单线程 $i" { & $seven a -bso0 -bsp0 "-t7z" '-mx=5' '-mmt1' $p "$src\*" | Out-Null }
}
$ours + $zip + $mt1 | ForEach-Object { '{0,-14} {1,7} ms' -f $_.组, $_.ms }
'--- 产物大小 (MB) ---'
foreach ($f in @('our1', 'our2', 'our3', 'ref1', 'ref2', 'ref3', 'one1', 'one2', 'one3')) {
  $fi = Get-Item "$work\$f.7z" -ErrorAction SilentlyContinue
  if ($fi) { '{0,-8} {1,7:N2}' -f $f, ($fi.Length / 1MB) }
}
'--- 我们这边的 stdout（含文件/目录数） ---'
& $exe --archivetest make $src "$work\probe.7z" 7z
