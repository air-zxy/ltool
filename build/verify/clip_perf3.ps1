$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
$log = [System.Collections.Generic.List[string]]::new()
function Say([string]$s) { $log.Add($s); Write-Host ('| ' + $s) }
function Flush { [System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\clip_perf3.txt', ($log -join "`n"), (New-Object System.Text.UTF8Encoding($false))) }
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
function Wait-IndexSettled([int]$timeoutSec) {
    $lg = Join-Path $env:APPDATA 'ltool\log.txt'
    $base = @(Get-Content $lg -ErrorAction SilentlyContinue).Count
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $timeoutSec) {
        Start-Sleep -Seconds 2
        $tail = @(Get-Content $lg | Select-Object -Skip $base)
        if (@($tail | Where-Object { $PSItem -match 'deep index|incremental ok' }).Count -gt 0) { Start-Sleep -Seconds 8; return $true }
    }
    return $false
}
function Sample([string]$label, [int]$seconds) {
    $p = Get-Process ltool -ErrorAction SilentlyContinue | Select-Object -First 1
    $p.Refresh(); $cpu0 = $p.TotalProcessorTime.TotalMilliseconds; $ws0 = $p.WorkingSet64
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $peak = 0
    while ($sw.Elapsed.TotalSeconds -lt $seconds) {
        Start-Sleep -Milliseconds 250; $p.Refresh()
        $d = $p.TotalProcessorTime.TotalMilliseconds - $cpu0
        if ($d -gt $peak) { $peak = $d }
    }
    Say ('{0}: cpu={1}ms/{2}s ({3}% of one core)  ws {4}MB -> {5}MB' -f $label, [math]::Round($peak), $seconds, [math]::Round($peak/($seconds*10),1), [math]::Round($ws0/1MB,1), [math]::Round($p.WorkingSet64/1MB,1))
}

$dir = Join-Path $env:APPDATA 'ltool'
$cfg = Join-Path $dir 'config.ini'
$store = Join-Path $dir 'clipboard.json'
$imgDir = Join-Path $dir 'clipimg'
Copy-Item $cfg "$cfg.perfbak" -Force
Say ('exe under test = ' + (Get-Item 'D:\project\cpp\ltool\build\Release\ltool.exe').LastWriteTime)

# worst case for the poll: a random-noise image, i.e. what a real screenshot costs to PNG-encode
$bmp = New-Object System.Drawing.Bitmap(2560, 1440)
$rect = New-Object System.Drawing.Rectangle 0,0,2560,1440
$data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadWrite, [System.Drawing.Imaging.PixelFormat]::Format32bppRgb)
$bytes = New-Object byte[] ($data.Stride * 1440)
(New-Object Random 42).NextBytes($bytes)
[System.Runtime.InteropServices.Marshal]::Copy($bytes, 0, $data.Scan0, $bytes.Length)
$bmp.UnlockBits($data)
$ms = New-Object System.IO.MemoryStream
$bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
Say ('noise image 2560x1440 PNG bytes = ' + $ms.ToArray().Length + ' (a real screenshot is in this range)')
$ms.Dispose()

Stop-Process -Name ltool -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
Remove-Item $store -Force -ErrorAction SilentlyContinue
if (Test-Path $imgDir) { Remove-Item "$imgDir\*.png" -Force -ErrorAction SilentlyContinue }
Set-Cfg 'clipboard' 'enable' 'true'
Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' | Out-Null
Say ('index settled = ' + (Wait-IndexSettled 120))
[System.Windows.Forms.Clipboard]::SetImage($bmp)
Start-Sleep -Seconds 3
Sample 'idle poll with a big image on the clipboard (history ON)' 30
Say ('stored image files = ' + @(Get-ChildItem $imgDir -Filter *.png -ErrorAction SilentlyContinue).Count)
$bmp.Dispose()

Stop-Process -Name ltool -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
Move-Item "$cfg.perfbak" $cfg -Force
Remove-Item $store -Force -ErrorAction SilentlyContinue
if (Test-Path $imgDir) { Remove-Item "$imgDir\*.png" -Force -ErrorAction SilentlyContinue }
Say 'cleanup done'
Start-Process -FilePath 'D:\software\ltool\ltool.exe'
Flush
Write-Host 'done'
