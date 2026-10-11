$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
$log = [System.Collections.Generic.List[string]]::new()
function Say([string]$s) { $log.Add($s); Write-Host ('| ' + $s) }
function Flush { [System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\clip2.txt', ($log -join "`n"), (New-Object System.Text.UTF8Encoding($false))) }
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
# PS5.1 gotchas: read UTF-8 explicitly, and pipe into ConvertFrom-Json (with -InputObject an
# array comes back as ONE object, which is what made the previous run's counts useless)
function Read-Items {
    $f = Join-Path $env:APPDATA 'ltool\clipboard.json'
    if (-not (Test-Path $f)) { return @() }
    return @(([IO.File]::ReadAllText($f, [Text.Encoding]::UTF8) | ConvertFrom-Json))
}
function Wait-For([scriptblock]$cond, [int]$secs) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $secs) {
        Start-Sleep -Milliseconds 400
        if (& $cond) { return [math]::Round($sw.Elapsed.TotalSeconds, 1) }
    }
    return -1
}

$dir = Join-Path $env:APPDATA 'ltool'
$cfg = Join-Path $dir 'config.ini'
$store = Join-Path $dir 'clipboard.json'
$imgDir = Join-Path $dir 'clipimg'
$saved = ''
try { if ([Windows.Forms.Clipboard]::ContainsText()) { $saved = [Windows.Forms.Clipboard]::GetText() } } catch { }
Copy-Item $cfg "$cfg.clipbak" -Force
Stop-Process -Name ltool -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
Remove-Item $store -Force -ErrorAction SilentlyContinue
if (Test-Path $imgDir) { Remove-Item "$imgDir\*.png" -Force -ErrorAction SilentlyContinue }
Set-Cfg 'clipboard' 'enable' 'true'
$p = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -PassThru
Start-Sleep -Seconds 5
Say ('pid=' + $p.Id + '  items at start=' + (Read-Items).Count)

# 1) exact text fidelity (ASCII, so nothing in the probe can mangle it)
[Windows.Forms.Clipboard]::SetText('AAA-111')
$d1 = Wait-For { @(Read-Items | Where-Object { $PSItem.text -eq 'AAA-111' }).Count -eq 1 } 12
Say ('1) "AAA-111" stored verbatim after ' + $d1 + 's')

# 2) re-copy the same text: must NOT create a second entry
[Windows.Forms.Clipboard]::SetText('BBB-222')
Wait-For { @(Read-Items | Where-Object { $PSItem.text -eq 'BBB-222' }).Count -eq 1 } 12 | Out-Null
[Windows.Forms.Clipboard]::SetText('AAA-111')
Start-Sleep -Seconds 6
$items = @(Read-Items)
$aaa = @($items | Where-Object { $PSItem.text -eq 'AAA-111' })
Say ('2) after re-copying AAA-111: total items=' + $items.Count + ' AAA copies=' + $aaa.Count + ' (expect 1)')
Say ('2) top item text=[' + $items[0].text + '] (expect AAA-111, i.e. moved to front)')

# 3) multiline text keeps its newlines
$multi = "L1`nL2`nL3"
[Windows.Forms.Clipboard]::SetText($multi)
$d3 = Wait-For { @(Read-Items | Where-Object { $PSItem.text -eq $multi }).Count -eq 1 } 12
Say ('3) multiline stored with newlines intact after ' + $d3 + 's')
$m = @(Read-Items | Where-Object { $PSItem.text -eq $multi })
if ($m.Count) { Say ('3) stored bytes=' + $m[0].bytes + ' (expect 8)') }

# 4) oversize text is skipped
[Windows.Forms.Clipboard]::SetText('z' * 300000)
Start-Sleep -Seconds 6
$after = @(Read-Items)
$big = @($after | Where-Object { $PSItem.text -and $PSItem.text.Length -gt 100000 })
Say ('4) items=' + $after.Count + ' (was ' + $items.Count + ') · oversize stored=' + ($big.Count -gt 0) + ' (expect same count / False)')

# 5) image
$bmp = New-Object System.Drawing.Bitmap(64, 48)
$g = [System.Drawing.Graphics]::FromImage($bmp); $g.Clear([System.Drawing.Color]::Coral); $g.Dispose()
[Windows.Forms.Clipboard]::SetImage($bmp)
$bmp.Dispose()
$d5 = Wait-For { @(Get-ChildItem $imgDir -Filter *.png -ErrorAction SilentlyContinue).Count -gt 0 } 12
$imgs = @(Read-Items | Where-Object { $PSItem.image -eq $true })
Say ('5) image after ' + $d5 + 's · image items=' + $imgs.Count)
if ($imgs.Count) { Say ('5) w=' + $imgs[0].w + ' h=' + $imgs[0].h + ' bytes=' + $imgs[0].bytes + ' file exists=' + (Test-Path $imgs[0].path)) }
Flush

Stop-Process -Name ltool -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
if ($saved.Length) { [Windows.Forms.Clipboard]::SetText($saved) }
Move-Item "$cfg.clipbak" $cfg -Force
Remove-Item $store -Force -ErrorAction SilentlyContinue
if (Test-Path $imgDir) { Remove-Item "$imgDir\*.png" -Force -ErrorAction SilentlyContinue }
Say 'cleanup: config restored, history + images removed, clipboard put back'
Start-Process -FilePath 'D:\software\ltool\ltool.exe'
Say 'restarted installed ltool'
Flush
Write-Host 'done'
