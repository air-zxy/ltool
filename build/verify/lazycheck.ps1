$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition (Get-Content -Raw 'D:\project\cpp\ltool\build\verify\clickguard_src.txt')
Add-Type -AssemblyName System.Drawing
$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force }
Start-Sleep -Milliseconds 700
$p = Start-Process -FilePath $exe -ArgumentList @('--settingspage', '1', '1') -PassThru
Start-Sleep -Seconds 8
$h = [ClickGuard]::Biggest([uint32]$p.Id)
if ($h -eq [IntPtr]::Zero) { 'NO WINDOW'; exit 1 }
$r = New-Object ClickGuard+RECT
[void][ClickGuard]::GetWindowRect($h, [ref]$r)
"window at $($r.l),$($r.t) size $(($r.r - $r.l))x(($r.b - $r.t))"

function Shot($file) {
  $rr = New-Object ClickGuard+RECT
  [void][ClickGuard]::GetWindowRect($script:h, [ref]$rr)
  $w = [int]($rr.r - $rr.l); $ht = [int]($rr.b - $rr.t)
  $bmp = [System.Drawing.Bitmap]::new($w, $ht)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  [void][ClickGuard]::PrintWindow($script:h, $hdc, 2)
  $g.ReleaseHdc($hdc)
  $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
  "saved $file"
}

Shot 'D:\project\cpp\ltool\build\verify\lazy_before.png'
# 第一行的"更多 ▾"：按上一张截图量到的客户区偏移
$x = $r.l + 1268
$y = $r.t + 190
$ok = [ClickGuard]::SafeClick($h, $x, $y)
"guarded click at $x,$y -> $ok"
if (-not $ok) { 'refused to click (front window is not ours)'; exit 1 }
Start-Sleep -Milliseconds 700
Shot 'D:\project\cpp\ltool\build\verify\lazy_menu.png'
[ClickGuard]::PressEsc()
Start-Sleep -Milliseconds 300
Stop-Process -Id $p.Id -Force
