$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Seq {
  [DllImport("user32.dll")] public static extern uint GetClipboardSequenceNumber();
}
"@
$log = [System.Collections.Generic.List[string]]::new()
function Say([string]$s) { $log.Add($s); Write-Host ('| ' + $s) }
function Flush { [System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\clip_perf.txt', ($log -join "`n"), (New-Object System.Text.UTF8Encoding($false))) }
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
function Sample([string]$label, [int]$seconds) {
    $p = Get-Process ltool -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $p) { Say "$label : no ltool process"; return }
    $cpu0 = $p.TotalProcessorTime.TotalMilliseconds
    $ws0 = $p.WorkingSet64
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $seconds) {
        Start-Sleep -Milliseconds 500
        $p.Refresh()
    }
    $cpu1 = $p.TotalProcessorTime.TotalMilliseconds
    $ws1 = $p.WorkingSet64
    Say ('{0}: cpu={1}ms over {2}s  ws {3}MB -> {4}MB  seq={5}' -f `
        $label, [math]::Round($cpu1 - $cpu0), $seconds, [math]::Round($ws0/1MB,1), [math]::Round($ws1/1MB,1), [Seq]::GetClipboardSequenceNumber())
}

$dir = Join-Path $env:APPDATA 'ltool'
$cfg = Join-Path $dir 'config.ini'
$store = Join-Path $dir 'clipboard.json'
$imgDir = Join-Path $dir 'clipimg'
Copy-Item $cfg "$cfg.perfbak" -Force
Stop-Process -Name ltool -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
Remove-Item $store -Force -ErrorAction SilentlyContinue
if (Test-Path $imgDir) { Remove-Item "$imgDir\*.png" -Force -ErrorAction SilentlyContinue }
Set-Cfg 'clipboard' 'enable' 'true'
$p = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -PassThru
Start-Sleep -Seconds 6

# A) idle baseline: a short text on the clipboard
[Windows.Forms.Clipboard]::SetText('idle-baseline')
Sample 'text-clipboard (enabled)' 12

# B) a big image sitting on the clipboard: the 2s poll re-reads and re-encodes it
$bmp = New-Object System.Drawing.Bitmap(2560, 1440)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$rng = New-Object Random 7
for ($y = 0; $y -lt 1440; $y += 8) {
  $c = [System.Drawing.Color]::FromRGB($rng.Next(0xFFFFFF) -bor 0x010101)
  $g.FillRectangle([System.Drawing.Brushes]::Transparent, 0, 0, 1, 1)
  $b = New-Object System.Drawing.SolidBrush $c
  $g.FillRectangle($b, 0, $y, 2560, 8); $b.Dispose()
}
$g.Dispose()
[System.Windows.Forms.Clipboard]::SetImage($bmp)
Say ('clipboard image = 2560x1440, PNG size on disk later')
Sample 'image-clipboard (enabled) 2560x1440' 20
$pngs = @(Get-ChildItem $imgDir -Filter *.png -ErrorAction SilentlyContinue)
Say ('stored images=' + $pngs.Count + ' totalKB=' + [math]::Round((($pngs | Measure-Object Length -Sum).Sum/1KB),1))

# C) same image, feature disabled: proves the gate
Stop-Process -Name ltool -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
Set-Cfg 'clipboard' 'enable' 'false'
$p3 = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -PassThru
Start-Sleep -Seconds 6
Sample 'image-clipboard (disabled)' 20

$bmp.Dispose()
Stop-Process -Name ltool -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
Move-Item "$cfg.perfbak" $cfg -Force
Remove-Item $store -Force -ErrorAction SilentlyContinue
if (Test-Path $imgDir) { Remove-Item "$imgDir\*.png" -Force -ErrorAction SilentlyContinue }
Say 'cleanup: config restored, test history/images removed'
Start-Process -FilePath 'D:\software\ltool\ltool.exe'
Say 'restarted installed ltool (clipboard switch back to its previous value)'
Flush
Write-Host 'done'
