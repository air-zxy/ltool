$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName System.Drawing
$dir = 'D:\project\cpp\ltool\build\verify\picktest'
if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
$out = Join-Path $dir 'text.png'

# 一张"字在已知坐标"的图：三行，行的基线位置是我画上去的，识别回来的框应该贴着它们
$lines = @(
  @{ x = 40; y = 20;  s = '软件安装路径' },
  @{ x = 40; y = 70;  s = '默认搜索引擎' },
  @{ x = 40; y = 120; s = 'Search Engine 123' }
)
$bmp = New-Object System.Drawing.Bitmap 640, 180
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::White)
$font = New-Object System.Drawing.Font('Microsoft YaHei UI', 22, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
foreach ($l in $lines) {
  $pt = New-Object System.Drawing.PointF ([single]$l.x), ([single]$l.y)
  $g.DrawString($l.s, $font, [System.Drawing.Brushes]::Black, $pt)
}
$g.Dispose()
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output ("图已生成 $out")
foreach ($l in $lines) { Write-Output ("画的：({0},{1}) {2}" -f $l.x, $l.y, $l.s) }
