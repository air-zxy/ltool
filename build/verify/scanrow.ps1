param([string]$png = 'D:\project\cpp\ltool\build\verify\lazy_before.png',
       [int]$y = 190)
Add-Type -AssemblyName System.Drawing
$bmp = [System.Drawing.Bitmap]::new($png)
$w = $bmp.Width
$runs = New-Object System.Collections.Generic.List[string]
$prev = $false
$start = -1
for ($x = 0; $x -lt $w; $x++) {
  $c = $bmp.GetPixel($x, $y)
  # 按钮底色比行背景亮：拿 R 通道阈值判断
  $isBtn = ($c.R -gt 45 -and $c.R -lt 90) -and ([Math]::Abs($c.R - $c.G) -lt 12) -and ([Math]::Abs($c.G - $c.B) -lt 20)
  if ($isBtn -and -not $prev) { $start = $x }
  if (-not $isBtn -and $prev) { $runs.Add(('{0}-{1} (w{2})' -f $start, ($x - 1), ($x - $start))) }
  $prev = $isBtn
}
"scan y=$y of $png  size ${w}x$($bmp.Height)"
$runs | ForEach-Object { '  run ' + $_ }
$px = @()
foreach ($dx in @(0, 40, 80, 120, 160, 200, 240, 280, 320)) {
  $x = [Math]::Min($w - 1, 1000 + $dx)
  $c = $bmp.GetPixel($x, $y)
  $px += ('x{0}=#{1:X2}{2:X2}{3:X2}' -f $x, $c.R, $c.G, $c.B)
}
'  samples: ' + ($px -join ' ')
$bmp.Dispose()
