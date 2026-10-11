$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition (Get-Content -Raw 'D:\project\cpp\ltool\build\verify\grab_src.txt')
Add-Type -AssemblyName System.Drawing
$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
$held = 'D:\project\cpp\ltool\build\verify\held.bin'
[System.IO.File]::WriteAllBytes($held, (New-Object byte[] (3MB)))

# 另起一个进程把这个文件一直开着，作为"占用者"
$locker = 'D:\project\cpp\ltool\build\verify\locker.ps1'
[System.IO.File]::WriteAllText($locker,
  "`$h=[System.IO.File]::Open('$held','Open','ReadWrite','None'); Start-Sleep -Seconds 40; `$h.Close()`n",
  (New-Object System.Text.UTF8Encoding($true)))
$lp = Start-Process powershell -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',$locker) -PassThru
Start-Sleep -Seconds 3
"locker pid=$($lp.Id)"

function ShotIt($args_, $file, $wait) {
  $p = Start-Process -FilePath $script:exe -ArgumentList $args_ -PassThru
  Start-Sleep -Seconds $wait
  $h = [Grab5]::Biggest([uint32]$p.Id)
  if ($h -eq [IntPtr]::Zero) { "NO WINDOW for $file"; return }
  $r = New-Object Grab5+RECT
  [void][Grab5]::GetWindowRect($h, [ref]$r)
  $w = [int]($r.r - $r.l); $ht = [int]($r.b - $r.t)
  $bmp = [System.Drawing.Bitmap]::new($w, $ht)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  [void][Grab5]::PrintWindow($h, $hdc, 2)
  $g.ReleaseHdc($hdc)
  $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
  "saved $file (${w}x${ht})"
  Stop-Process -Id $p.Id -Force
}

ShotIt @('--fileinfo', $held) 'D:\project\cpp\ltool\build\verify\ui_fileinfo.png' 3
ShotIt @('--whohas', $held) 'D:\project\cpp\ltool\build\verify\ui_whohas.png' 6
ShotIt @('--shred', $held) 'D:\project\cpp\ltool\build\verify\ui_shred.png' 3
"held file still exists (should be True, nothing was clicked): $([bool](Test-Path $held))"
Stop-Process -Id $lp.Id -Force
Remove-Item $held -Force -ErrorAction SilentlyContinue
