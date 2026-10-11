$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition (Get-Content -Raw 'D:\project\cpp\ltool\build\verify\grab_src.txt')
$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force }
Start-Sleep -Milliseconds 700
$p = Start-Process -FilePath $exe -PassThru
Start-Sleep -Seconds 4
$null = Start-Process -FilePath $exe -ArgumentList '--show' -PassThru
Start-Sleep -Seconds 2
$h = [Grab5]::Biggest([uint32]$p.Id)
if ($h -eq [IntPtr]::Zero) { 'NO WINDOW'; exit 1 }
$r = New-Object Grab5+RECT
[void][Grab5]::GetWindowRect($h, [ref]$r)
$w = [int]($r.r - $r.l)
$ht = [int]($r.b - $r.t)
"launcher rect: x=$($r.l) y=$($r.t) ${w}x${ht}"
$bmp = [System.Drawing.Bitmap]::new($w, $ht)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
[void][Grab5]::PrintWindow($h, $hdc, 2)
$g.ReleaseHdc($hdc)
$bmp.Save('D:\project\cpp\ltool\build\verify\launcher_warm.png', [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
'saved'
Get-Content (Join-Path $env:APPDATA 'ltool\log.txt') -Encoding UTF8 -Tail 4
