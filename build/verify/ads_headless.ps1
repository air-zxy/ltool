$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class W32c {
  [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, char[] s, int n);
}
"@
function Ch([int[]]$c) { $s = ''; foreach ($x in $c) { $s += [string][char]$x }; return $s }
$T_ADTITLE = Ch @(0x4ECA, 0x65E5, 0x70ED, 0x70B9, 0x63A8, 0x5E7F)   # 今日热点推广：命中默认标题名单

$log = [System.Collections.Generic.List[string]]::new()
function Say([string]$s) { $log.Add($s); Write-Host ('| ' + $s) }
function Flush { [System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\ads_headless.txt', ($log -join "`n"), (New-Object System.Text.UTF8Encoding($false))) }
function Pump([int]$ms) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $ms) {
        [System.Windows.Forms.Application]::DoEvents()
        Start-Sleep -Milliseconds 50
    }
}
function Alive([IntPtr]$h) { return [W32c]::IsWindow($h) }
function TitleOf([IntPtr]$h) {
    $buf = New-Object char[] 256
    $n = [W32c]::GetWindowTextW($h, $buf, 256)
    if ($n -le 0) { return '' }
    return (New-Object string($buf, 0, $n))
}
# a fake ad window owned by THIS script; we keep the HWND so a disposed Form cannot hide the result
function New-FakeAd {
    $f = New-Object System.Windows.Forms.Form
    $f.Text = $T_ADTITLE
    $f.Size = New-Object System.Drawing.Size(420, 300)
    $f.StartPosition = 'Manual'
    $f.Location = New-Object System.Drawing.Point(140, 140)
    $f.TopMost = $true
    $f.Add_FormClosed({ param($s, $e) $script:closedAt = (Get-Date).ToString('HH:mm:ss') })
    $f.Show()
    [System.Windows.Forms.Application]::DoEvents()
    return @{ form = $f; hwnd = $f.Handle }
}
function Stop-Ltool { Stop-Process -Name ltool -Force -ErrorAction SilentlyContinue; Start-Sleep -Seconds 2 }
function Start-Ltool {
    $p = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -PassThru
    Start-Sleep -Seconds 4
    return $p
}
function Read-Utf8Json([string]$path) {
    # PS5.1's Get-Content decodes BOM-less files as ANSI(GBK) and mangles the Chinese titles,
    # which then fails ConvertFrom-Json. Read the bytes as UTF-8 explicitly.
    return [System.IO.File]::ReadAllText($path, [System.Text.Encoding]::UTF8)
}
function Count-Hist {
    if (-not (Test-Path $script:hist)) { return -1 }
    $arr = Read-Utf8Json $script:hist | ConvertFrom-Json
    if (-not $arr) { return 0 }
    return @($arr).Count
}
function Set-Cfg([string]$section, [string]$key, [string]$val) {
    $path = Join-Path $env:APPDATA 'ltool\config.ini'
    $lines = @(Get-Content $path -ErrorAction SilentlyContinue)
    $out = [System.Collections.Generic.List[string]]::new()
    $secIdx = -1
    for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i].Trim() -eq ('[' + $section + ']')) { $secIdx = $i; break } }
    if ($secIdx -lt 0) {
        foreach ($l in $lines) { $out.Add($l) }
        $out.Add(''); $out.Add('[' + $section + ']'); $out.Add("$key=$val")
    } else {
        $end = $lines.Count
        for ($i = $secIdx + 1; $i -lt $lines.Count; $i++) { if ($lines[$i].Trim().StartsWith('[')) { $end = $i; break } }
        $hit = $false
        for ($i = 0; $i -lt $lines.Count; $i++) {
            if ($i -gt $secIdx -and $i -lt $end -and $lines[$i] -match ('^\s*' + [regex]::Escape($key) + '\s*=')) {
                $out.Add("$key=$val"); $hit = $true; continue
            }
            $out.Add($lines[$i])
        }
        if (-not $hit) { $out.Insert($secIdx + 1, "$key=$val") }
    }
    [System.IO.File]::WriteAllLines($path, $out, (New-Object System.Text.UTF8Encoding($false)))
}
function Write-Rules([object]$obj) {
    # no BOM: keep the file byte-shape identical to what Qt writes itself
    [System.IO.File]::WriteAllText($script:rules, ($obj | ConvertTo-Json -Depth 4), (New-Object System.Text.UTF8Encoding($false)))
}

$cfg = Join-Path $env:APPDATA 'ltool\config.ini'
$script:rules = Join-Path $env:APPDATA 'ltool\adblock.json'
$script:hist = Join-Path $env:APPDATA 'ltool\adhistory.json'
Copy-Item $cfg "$cfg.adsbak" -Force -ErrorAction SilentlyContinue
if (Test-Path $script:rules) { Copy-Item $script:rules "$($script:rules).adsbak" -Force }
Remove-Item $script:hist -Force -ErrorAction SilentlyContinue
Say 'backed up config.ini + adblock.json, removed adhistory.json'

# ============ phase 1: auto on, no whitelist -> window must be closed and recorded ============
Stop-Ltool
Set-Cfg 'adblock' 'auto' 'true'
Set-Cfg 'adblock' 'observe' 'false'
$p = Start-Ltool
Say ('--- phase1: pid=' + $p.Id + ' records=' + (Count-Hist))
$a1 = New-FakeAd
$script:closedAt = ''
Say ('phase1 hwnd=' + $a1.hwnd + ' title=[' + (TitleOf $a1.hwnd) + '] alive=' + (Alive $a1.hwnd))
Pump 20000
Say ('phase1 after 20s (4 ticks): alive=' + (Alive $a1.hwnd) + ' closedAt=' + $script:closedAt + ' records=' + (Count-Hist))
if (Test-Path $script:hist) { Say ('phase1 history=' + (Read-Utf8Json $script:hist)) }
Flush

# ============ phase 2: whitelisted process -> window must survive, records must NOT grow ====
Stop-Ltool
$j = (Read-Utf8Json $script:rules | ConvertFrom-Json)
$ap = @('powershell')
$j | Add-Member -NotePropertyName allow_proc -NotePropertyValue $ap -Force
Write-Rules $j
Say ('--- phase2 rules allow_proc json=' + (((Read-Utf8Json $script:rules | ConvertFrom-Json)).allow_proc -join '|'))
$before2 = Count-Hist
$p2 = Start-Ltool
$a2 = New-FakeAd
$script:closedAt = ''
Say ('phase2 hwnd=' + $a2.hwnd + ' alive=' + (Alive $a2.hwnd) + ' records before=' + $before2)
Pump 20000
Say ('phase2 after 20s: alive=' + (Alive $a2.hwnd) + ' closedAt=[' + $script:closedAt + '] records=' + (Count-Hist) + ' (expect alive + no new records)')
$a2.form.Close()      # don't let it linger: phase 3 clears the whitelist and would close+record it
Pump 500
Flush

# ============ phase 3: history must survive a restart ============
Stop-Ltool
$j3 = (Read-Utf8Json $script:rules | ConvertFrom-Json)
$j3 | Add-Member -NotePropertyName allow_proc -NotePropertyValue @() -Force
Write-Rules $j3
$before3 = Count-Hist
$p3 = Start-Ltool
Start-Sleep -Seconds 2
Say ('--- phase3 records visible after restart=' + (Count-Hist) + ' (before restart it was ' + $before3 + ')')
$base3 = Count-Hist
$a3 = New-FakeAd
$script:closedAt = ''
Pump 20000
Say ('phase3 new window after 20s: alive=' + (Alive $a3.hwnd) + ' records=' + (Count-Hist) + ' (expect closed + records=' + ($base3 + 1) + ')')
if (Test-Path $script:hist) { Say ('phase3 history=' + (Read-Utf8Json $script:hist)) }
Flush

# ============ cleanup ============
foreach ($x in @($a1, $a2, $a3)) { if ($x -and $x.form) { try { $x.form.Close() } catch { } } }
Pump 500
Stop-Ltool
if (Test-Path "$cfg.adsbak") { Move-Item "$cfg.adsbak" $cfg -Force }
if (Test-Path "$($script:rules).adsbak") { Move-Item "$($script:rules).adsbak" $script:rules -Force }
Remove-Item $script:hist -Force -ErrorAction SilentlyContinue
Say 'restored config.ini + adblock.json, removed test adhistory.json'
Start-Process -FilePath 'D:\software\ltool\ltool.exe'
Say 'restarted the installed ltool'
Flush
Write-Host 'done'
