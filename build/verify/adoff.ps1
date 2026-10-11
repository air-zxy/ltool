$ErrorActionPreference = 'Stop'
$cfg = Join-Path $env:APPDATA 'ltool\config.ini'
$btc = "$cfg.off-auto.bak"
function Read-Utf8([string]$p) { return [System.IO.File]::ReadAllText($p, [System.Text.Encoding]::UTF8) }

Write-Output '--- before ---'
Write-Output (Read-Utf8 $cfg)
Copy-Item $cfg $btc -Force
Write-Output ('backed up to ' + $btc)

$lines = @(Get-Content $cfg)
$secIdx = -1
for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i].Trim() -eq '[adblock]') { $secIdx = $i; break } }
if ($secIdx -lt 0) { throw 'no [adblock] section: refusing to guess' }
$end = $lines.Count
for ($i = $secIdx + 1; $i -lt $lines.Count; $i++) { if ($lines[$i].Trim().StartsWith('[')) { $end = $i; break } }
$out = [System.Collections.Generic.List[string]]::new()
$hit = $false
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($i -gt $secIdx -and $i -lt $end -and $lines[$i] -match '^\s*auto\s*=') { $out.Add('auto=false'); $hit = $true; continue }
    $out.Add($lines[$i])
}
if (-not $hit) { $out.Insert($secIdx + 1, 'auto=false') }
[System.IO.File]::WriteAllLines($cfg, $out, (New-Object System.Text.UTF8Encoding($false)))
Write-Output '--- after (file) ---'
Write-Output (Read-Utf8 $cfg)

# 运行中的实例只在启动时读一次这个开关，重启才生效
Stop-Process -Name ltool -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
Start-Process -FilePath 'D:\software\ltool\ltool.exe'
Start-Sleep -Seconds 6
Write-Output '--- log after restart ---'
Get-Content (Join-Path $env:APPDATA 'ltool\log.txt') | Select-Object -Last 4 | ForEach-Object { Write-Output $_ }
