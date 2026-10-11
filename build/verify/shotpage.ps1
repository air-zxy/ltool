$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition (Get-Content -Raw 'D:\project\cpp\ltool\build\verify\grab_src.txt')
Add-Type -AssemblyName System.Drawing
$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
$row = $args[0]
$tab = $args[1]
$outPng = $args[2]
Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force }
Start-Sleep -Milliseconds 600
$p = Start-Process -FilePath $exe -ArgumentList @('--settingspage', $row, $tab) -PassThru
Start-Sleep -Seconds 5
$h = [Grab5]::Biggest([uint32]$p.Id)
if ($h -eq [IntPtr]::Zero) { "NO WINDOW pid=$($p.Id) exited=$($p.HasExited)"; exit 1 }
$r = New-Object Grab5+RECT
[void][Grab5]::GetWindowRect($h, [ref]$r)
"rect $(($r.r - $r.l))x(($r.b - $r.t))"
$bmp = [System.Drawing.Bitmap]::new([int]($r.r - $r.l), [int]($r.b - $r.t))
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
$ok = [Grab5]::PrintWindow($h, $hdc, 2)
$g.ReleaseHdc($hdc)
$bmp.Save($outPng, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
"saved ok=$ok -> $outPng"
Stop-Process -Id $p.Id -Force
