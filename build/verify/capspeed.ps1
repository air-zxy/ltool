$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

$bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
Write-Output ('screen = ' + $bounds.Width + 'x' + $bounds.Height)

function TimeIt($label, $w, $h, $n) {
  $times = New-Object 'System.Collections.Generic.List[double]'
  for ($k = 0; $k -lt $n; $k++) {
    $bmp = New-Object System.Drawing.Bitmap($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $g.CopyFromScreen(0, 0, 0, 0, $bmp.Size)
    $sw.Stop()
    $times.Add($sw.Elapsed.TotalMilliseconds)
    $g.Dispose()
    $bmp.Dispose()
  }
  $sorted = $times | Sort-Object
  $med = $sorted[[int]($n / 2)]
  $fps = 1000.0 / $med
  Write-Output ($label + ': min=' + [math]::Round($sorted[0], 1) + 'ms med=' + [math]::Round($med, 1) `
    + 'ms max=' + [math]::Round($sorted[$n - 1], 1) + 'ms  -> 单帧中位 ' + [math]::Round($fps, 1) + ' fps 上限')
}

TimeIt 'full 2560x1440' $bounds.Width $bounds.Height 12
TimeIt 'half 1280x720' 1280 720 12
TimeIt 'small 800x600' 800 600 12

# 顺带看一眼 JPEG 编码一屏要多久（如果走 MJPEG/AVI 这条路就要它）
$bmp2 = New-Object System.Drawing.Bitmap($bounds.Width, $bounds.Height)
$g2 = [System.Drawing.Graphics]::FromImage($bmp2)
$g2.CopyFromScreen(0, 0, 0, 0, $bmp2.Size)
$g2.Dispose()
$sw2 = [System.Diagnostics.Stopwatch]::StartNew()
$ms = New-Object System.IO.MemoryStream
$bmp2.Save($ms, [System.Drawing.Imaging.ImageFormat]::Jpeg)
$sw2.Stop()
Write-Output ('jpeg encode full screen: ' + [math]::Round($sw2.Elapsed.TotalMilliseconds, 1) + 'ms, ' + [math]::Round($ms.Length / 1KB, 0) + ' KB')
$bmp2.Dispose()
