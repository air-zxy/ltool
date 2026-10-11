$ErrorActionPreference = 'Stop'
# Screenshot the settings page with the window made tall enough to see the bottom groups.
# args: <pageRow> <outPng> ; ASCII only (PS 5.1 without BOM decodes as GBK and eats newlines)
Add-Type -TypeDefinition (Get-Content -Raw 'D:\project\cpp\ltool\build\verify\grab_src.txt')
Add-Type -AssemblyName System.Drawing
$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
$row = $args[0]
$outPng = $args[1]
Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force }
Start-Sleep -Milliseconds 700
$p = Start-Process -FilePath $exe -ArgumentList @('--settingspage', $row) -PassThru
Start-Sleep -Seconds 5
$h = [Grab5]::Biggest([uint32]$p.Id)
if ($h -eq [IntPtr]::Zero) { "NO WINDOW pid=$($p.Id)"; exit 1 }
$r = New-Object Grab5+RECT
[void][Grab5]::GetWindowRect($h, [ref]$r)
# pull it down to the top-left and make it tall so the last groups are on screen
[void][Grab5]::MoveWindow($h, 20, 10, [Math]::Max(980, $r.r - $r.l), 1360, $true)
Start-Sleep -Milliseconds 900
[void][Grab5]::GetWindowRect($h, [ref]$r)
$w = $r.r - $r.l; $ht = $r.b - $r.t
$bmp = [System.Drawing.Bitmap]::new([int]$w, [int]$ht)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
$ok = [Grab5]::PrintWindow($h, $hdc, 2)
$g.ReleaseHdc($hdc)
$bmp.Save($outPng, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
"saved ${w}x${ht} ok=$ok -> $outPng"
Stop-Process -Id $p.Id -Force
