$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
$log = [System.Collections.Generic.List[string]]::new()
function Say([string]$s) { $log.Add($s); Write-Host ('| ' + $s) }
function Flush { [System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\clip_perf2.txt', ($log -join "`n"), (New-Object System.Text.UTF8Encoding($false))) }
function Set-Cfg([string]$section, [string]$key, [string]$val) {
    $path = Join-Path $env:APPDATA 'ltool\config.ini'
    $lines = @(Get-Content $path -ErrorAction SilentlyContinue)
    $out = [System.Collections.Generic.List[string]]::new()
    $secIdx = -1
    for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i].Trim() -eq ('[' + $section + ']')) { $secIdx = $i; break } }
    if ($secIdx -lt 0) { foreach ($l in $lines) { $out.Add($l) }; $out.Add(''); $out.Add("[$section]"); $out.Add("$key=$val") }
    else {
        $end = $lines.Count
        for ($i = $secIdx + 1; $i -lt $lines.Count; $i++) { if ($lines[$i].Trim().StartsWith('[')) { $end = $i; break } }
        $hit = $false
        for ($i = 0; $i -lt $lines.Count; $i++) {
            if ($i -gt $secIdx -and $i -lt $end -and $lines[$i] -match ('^\s*' + [regex]::Escape($key) + '\s*=')) { $out.Add("$key=$val"); $hit = $true; continue }
            $out.Add($lines[$i])
        }
        if (-not $hit) { $out.Insert($secIdx + 1, "$key=$val") }
    }
    [System.IO.File]::WriteAllLines($path, $out, (New-Object System.Text.UTF8Encoding($false)))
}
# the app rebuilds the deep index for ~20s after every start; sampling in that window
# measures the indexer, not the clipboard. Wait for the "deep index full rebuild" line.
function Wait-IndexSettled([int]$timeoutSec) {
    $lg = Join-Path $env:APPDATA 'ltool\log.txt'
    $base = @(Get-Content $lg -ErrorAction SilentlyContinue).Count
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $timeoutSec) {
        Start-Sleep -Seconds 2
        $tail = @(Get-Content $lg | Select-Object -Skip $base)
        if (@($tail | Where-Object { $PSItem -match 'deep index|incremental ok' }).Count -gt 0) {
            Start-Sleep -Seconds 8      # let the working-set trim run
            return $true
        }
    }
    return $false
}
function Sample([string]$label, [int]$seconds) {
    $p = Get-Process ltool -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $p) { Say "$label : no ltool process"; return }
    $p.Refresh(); $cpu0 = $p.TotalProcessorTime.TotalMilliseconds; $ws0 = $p.WorkingSet64
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $seconds) { Start-Sleep -Milliseconds 500; $p.Refresh() }
    $cpu1 = $p.TotalProcessorTime.TotalMilliseconds; $ws1 = $p.WorkingSet64
    Say ('{0}: cpu={1}ms/{2}s  ws {3}MB -> {4}MB' -f $label, [math]::Round($cpu1 - $cpu0), $seconds, [math]::Round($ws0/1MB,1), [math]::Round($ws1/1MB,1))
}

$dir = Join-Path $env:APPDATA 'ltool'
$cfg = Join-Path $dir 'config.ini'
$store = Join-Path $dir 'clipboard.json'
$imgDir = Join-Path $dir 'clipimg'
Copy-Item $cfg "$cfg.perfbak" -Force

$bmp = New-Object System.Drawing.Bitmap(2560, 1440)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$rng = New-Object Random 7
for ($y = 0; $y -lt 1440; $y += 4) {
    $v = $rng.Next(256); $b = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, $v, ($v -shr 1) -bxor 90, 255 - $v))
    $g.FillRectangle($b, 0, $y, 2560, 4); $b.Dispose()
}
$g.Dispose()
$ms = New-Object System.IO.MemoryStream
$bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
$pngLen = $ms.ToArray().Length
$ms.Dispose()
Say ('test image 2560x1440, PNG bytes = ' + $pngLen)

foreach ($on in @($true, $false)) {
    Stop-Process -Name ltool -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
    Remove-Item $store -Force -ErrorAction SilentlyContinue
    if (Test-Path $imgDir) { Remove-Item "$imgDir\*.png" -Force -ErrorAction SilentlyContinue }
    Set-Cfg 'clipboard' 'enable' ($(if ($on) { 'true' } else { 'false' }))
    Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' | Out-Null
    $settled = Wait-IndexSettled 120
    Say ('index settled before sampling = ' + $settled)
    [Windows.Forms.Clipboard]::SetImage($bmp)
    Start-Sleep -Seconds 3
    Sample ('image on clipboard, clipboard-history=' + $on) 30
    $pngs = @(Get-ChildItem $imgDir -Filter *.png -ErrorAction SilentlyContinue)
    Say ('   stored image files = ' + $pngs.Count)
}

Stop-Process -Name ltool -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
Move-Item "$cfg.perfbak" $cfg -Force
Remove-Item $store -Force -ErrorAction SilentlyContinue
if (Test-Path $imgDir) { Remove-Item "$imgDir\*.png" -Force -ErrorAction SilentlyContinue }
Say 'cleanup: config restored, test history/images removed'
Start-Process -FilePath 'D:\software\ltool\ltool.exe'
Say 'restarted installed ltool'
$bmp.Dispose()
Flush
Write-Host 'done'
