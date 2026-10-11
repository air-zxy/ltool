$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition (Get-Content -Raw 'D:\project\cpp\ltool\build\verify\grab_src.txt')
$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
$cfg = Join-Path $env:APPDATA 'ltool\config.ini'
$bbk = 'D:\project\cpp\ltool\build\verify\config.ini.bak'
Copy-Item $cfg $bbk -Force

function Shot($file) {
  $h = [Grab5]::Biggest([uint32]$script:pid_)
  if ($h -eq [IntPtr]::Zero) { "NO WINDOW for $file"; return }
  $r = New-Object Grab5+RECT
  [void][Grab5]::GetWindowRect($h, [ref]$r)
  $w = [int]($r.r - $r.l); $ht = [int]($r.b - $r.t)
  "$file rect ${w}x${ht}"
  $bmp = [System.Drawing.Bitmap]::new($w, $ht)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  [void][Grab5]::PrintWindow($h, $hdc, 2)
  $g.ReleaseHdc($hdc)
  $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
}

function Launch($title) {
  Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force }
  Start-Sleep -Milliseconds 700
  $p = Start-Process -FilePath $exe -PassThru
  $script:pid_ = $p.Id
  Start-Sleep -Seconds 3
  $null = Start-Process -FilePath $exe -ArgumentList '--show' -PassThru
  Start-Sleep -Seconds 2
  "=== $title ==="
}

$lines = Get-Content $cfg -Encoding UTF8
"config has favoritesOnEmpty? " + [bool]($lines -match 'favoritesOnEmpty')
Launch '默认（不勾）：只应有搜索框'
Shot 'D:\project\cpp\ltool\build\verify\fav_off.png'
Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force }

# 追加式写入开关（不覆盖整个文件）
$txt = [System.IO.File]::ReadAllText($cfg)
if ($txt -match '(?m)^\[ui\]') {
  $txt2 = [regex]::Replace($txt, '(?m)^(\[ui\]\r?\n)', "`$1favoritesOnEmpty=true`r`n")
} else {
  $txt2 = $txt.TrimEnd() + "`r`n`r`n[ui]`r`nfavoritesOnEmpty=true`r`n"
}
[System.IO.File]::WriteAllText($cfg + '.new', $txt2)
Move-Item -Force ($cfg + '.new') $cfg
'appended favoritesOnEmpty=true'

Launch '勾上以后：应展开常用列表'
Shot 'D:\project\cpp\ltool\build\verify\fav_on.png'
Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force }
Copy-Item $bbk $cfg -Force
'config restored from backup'
