$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class W32 {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
  [DllImport("user32.dll")] public static extern IntPtr GetWindowFromPoint(POINT p);
  [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h, uint flags);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool SetWindowText(IntPtr h, string s);
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int hh, bool r);
  [DllImport("user32.dll")] public static extern IntPtr FindWindow(string cls, string title);
  [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
}
"@
function Ch([int[]]$c) { $s = ''; foreach ($x in $c) { $s += [string][char]$x }; return $s }
# needles, built from code points so this file stays pure ASCII
$T_ADS     = Ch @(0x5E7F, 0x544A, 0x5F39, 0x7A97)                       # menu: ad popup page
$T_ALLOW   = Ch @(0x52A0, 0x5165, 0x767D, 0x540D, 0x5355)               # add to whitelist
$T_DRY     = Ch @(0x8BD5, 0x8DD1, 0x89C4, 0x5219)                       # dry run rules
$T_RULES   = Ch @(0x7F16, 0x8F91, 0x89C4, 0x5219)                       # edit rules
$T_ALLOWW  = Ch @(0x767D, 0x540D, 0x5355)                               # "whitelist" (in summary)
$T_AUTO    = Ch @(0x81EA, 0x52A8, 0x62E6, 0x622A)                       # "auto intercept" checkbox
$T_ON      = Ch @(0x5F00)                                               # on
$T_ADTITLE = Ch @(0x4ECA, 0x65E5, 0x70ED, 0x70B9, 0x63A8, 0x5E7F)       # fake ad window title
$T_STATS   = Ch @(0x6700, 0x8FD1, 0x0020)                               # "recent " (stats line)
$T_RESCAN  = Ch @(0x91CD, 0x65B0, 0x626B, 0x63CF)                       # rescan
$T_HIST    = Ch @(0x5237, 0x65B0, 0x8BB0, 0x5F55)                       # refresh records

$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$log = [System.Collections.Generic.List[string]]::new()
function Say([string]$s) { $log.Add($s); Write-Host ('| ' + $s) }
function Flush { [System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\ads_e2e.txt', ($log -join "`r`n"), [System.Text.Encoding]::UTF8) }
function Get-Kids([object]$w, [object]$t) {
    return $w.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $t)))
}
function Raise([object]$w) {
    for ($i = 0; $i -lt 8; $i++) {
        [W32]::keybd_event(0x12, 0, 0, [IntPtr]::Zero)
        [W32]::keybd_event(0x12, 0, 2, [IntPtr]::Zero)
        [W32]::SetForegroundWindow([IntPtr]$w.Current.NativeWindowHandle) | Out-Null
        Start-Sleep -Milliseconds 300
        if ([W32]::GetForegroundWindow() -eq [IntPtr]$w.Current.NativeWindowHandle) { return $true }
    }
    return $false
}
# a click is only allowed when the point belongs to our own top-level window
function Click([object]$w, [object]$el) {
    if (-not (Raise $w)) { Say 'SKIP CLICK: foreground not acquired'; return $false }
    $r = $el.Current.BoundingRectangle
    $pt = New-Object System.Drawing.Point ([int]($r.X + $r.Width / 2)), ([int]($r.Y + $r.Height / 2))
    $hit = [W32]::GetAncestor([W32]::GetWindowFromPoint($pt), 2)   # GA_ROOT
    if ($hit -ne [IntPtr]$w.Current.NativeWindowHandle) { Say ('SKIP CLICK: point lands on another window (' + $hit + ')'); return $false }
    [W32]::SetCursorPos($pt.X, $pt.Y) | Out-Null
    Start-Sleep -Milliseconds 150
    [W32]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 60
    [W32]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 500
    return $true
}
function Find-Item([object]$w, [string]$name, [object]$type) {
    foreach ($e in (Get-Kids $w $type)) { if ($e.Current.Name.StartsWith($name)) { return $e } }
    return $null
}
function Shot([object]$w, [string]$file) {
    $r = $w.Current.BoundingRectangle
    $bmp = New-Object System.Drawing.Bitmap([int]$r.Width, [int]$r.Height)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen([int]$r.X, [int]$r.Y, 0, 0, $bmp.Size)
    $g.Dispose()
    $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}
function Dump-Page([object]$w, [string]$tag) {
    Say "--- $tag : texts ---"
    foreach ($t in (Get-Kids $w $CT::Text)) {
        $n = $t.Current.Name
        if ($n) { foreach ($line in ($n -split "`n")) { Say ('  txt: ' + $line) } }
    }
    Say "--- $tag : list items ---"
    foreach ($li in (Get-Kids $w $CT::ListItem)) {
        $n = $li.Current.Name
        if ($n -and $n.Length -gt 1 -and $n -ne $T_ADS) { Say ('  item: ' + $n) }
    }
}

if (Get-Process ltool -ErrorAction SilentlyContinue) { Say 'ABORT: another ltool running'; Flush; exit 1 }
$proc = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -ArgumentList '--settings' -PassThru
Say ('pid=' + $proc.Id)
$win = $null
for ($i = 0; $i -lt 25; $i++) {
    Start-Sleep -Milliseconds 800
    if ($proc.HasExited) { Say 'ABORT: exited early'; Flush; exit 1 }
    $win = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst(
        $TC::Children, (New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)))
    if ($win) { break }
}
if (-not $win) { Say 'ABORT: no window'; Flush; exit 1 }
Start-Sleep -Seconds 2

# --- go to the ad page (Qt list items need a real click, and the switch is async) ---
$onAds = $false
for ($try = 1; $try -le 3 -and -not $onAds; $try++) {
    $item = $null
    foreach ($li in (Get-Kids $win $CT::ListItem)) { if ($li.Current.Name -eq $T_ADS) { $item = $li } }
    if (-not $item) { Say 'no ad menu entry'; break }
    Click $win $item | Out-Null
    for ($i = 0; $i -lt 12 -and -not $onAds; $i++) {
        Start-Sleep -Milliseconds 500
        foreach ($t in (Get-Kids $win $CT::Text)) {
            if ($t.Current.Name -and $t.Current.Name.Contains($T_ALLOWW)) { $onAds = $true; break }
        }
    }
    Say ('ad page attempt ' + $try + ' -> ' + $onAds)
}
if (-not $onAds) { Say 'STUCK: never reached the ad page'; Flush; exit 1 }
Dump-Page $win 'PAGE-INITIAL'
Shot $win 'D:\project\cpp\ltool\build\verify\ads_page.png'
Flush

# --- buttons present? ---
$names = @{}
foreach ($b in (Get-Kids $win $CT::Button)) { if ($b.Current.Name) { $names[$b.Current.Name] = 1 } }
Say '--- buttons ---'
foreach ($k in $names.Keys) { Say ('  btn: ' + $k) }
Say ('  has whitelist btn = ' + ($names.ContainsKey($T_ALLOW)))
Say ('  has dry-run btn     = ' + ($names.ContainsKey($T_DRY)))
Flush

# --- fake ad window: a form owned by THIS script (notepad on Win11 is a store stub, no usable pid) ---
function New-FakeAd([string]$title) {
    $f = New-Object System.Windows.Forms.Form
    $f.Text = $title
    $f.Size = New-Object System.Drawing.Size(420, 300)
    $f.StartPosition = 'Manual'
    $f.Location = New-Object System.Drawing.Point(140, 140)
    $f.TopMost = $true
    $f.Show()
    [System.Windows.Forms.Application]::DoEvents()
    return $f
}
function Pump([int]$ms) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $ms) {
        [System.Windows.Forms.Application]::DoEvents()
        Start-Sleep -Milliseconds 50
    }
}
$ad1 = New-FakeAd $T_ADTITLE
$hwnd = $ad1.Handle
Say ('fake ad window hwnd=' + $hwnd + ' title=' + $ad1.Text + ' size=' + $ad1.Width + 'x' + $ad1.Height)
Start-Sleep -Seconds 1
Dump-Page $win 'AFTER-FAKE-AD'
Flush

# --- turn on auto intercept, wait for the 5s tick ---
$cb = $null
foreach ($c in (Get-Kids $win $CT::CheckBox)) { if ($c.Current.Name.StartsWith($T_AUTO)) { $cb = $c } }
if (-not $cb) { Say 'ABORT: auto checkbox not found'; Flush; exit 1 }
Say ('auto checkbox before = ' + $cb.Current.Name)
Click $win $cb | Out-Null
Start-Sleep -Milliseconds 500
Pump 1000
foreach ($c in (Get-Kids $win $CT::CheckBox)) { if ($c.Current.Name.StartsWith($T_AUTO)) { Say ('auto checkbox after = ' + $c.Current.Name) } }
Say 'waiting 10s for two interceptor ticks (pumping so our fake window can actually receive WM_CLOSE)...'
Pump 10000
Say ('our fake ad window still alive = ' + [W32]::IsWindow($hwnd))
$hist = Join-Path $env:APPDATA 'ltool\adhistory.json'
Say ('adhistory.json exists = ' + (Test-Path $hist))
if (Test-Path $hist) {
    $raw = Get-Content $hist -Raw
    Say ('adhistory.json bytes = ' + $raw.Length)
    Say ('adhistory.json head = ' + $raw.Substring(0, [Math]::Min(400, $raw.Length)))
}
Dump-Page $win 'AFTER-AUTO-ON'
Shot $win 'D:\project\cpp\ltool\build\verify\ads_after_close.png'
Flush

# --- whitelist path: a second fake ad must SURVIVE after 加入白名单 ---
$ad2 = New-FakeAd $T_ADTITLE
$h2 = $ad2.Handle
Say ('second fake ad hwnd=' + $h2)
Click $win (Find-Item $win $T_RESCAN $CT::Button) | Out-Null
Pump 2000
Dump-Page $win 'BEFORE-WHITELIST'
$hitList = $null
foreach ($li in (Get-Kids $win $CT::List)) { if (-not $hitList) { $hitList = $li } }
$first = $null
if ($hitList) {
    $kids = $hitList.FindAll($TC::Children, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $CT::ListItem)))
    if ($kids.Count -gt 0) { $first = $kids[0] }
}
if ($first) {
    Say ('selecting hit row: ' + $first.Current.Name)
    Click $win $first | Out-Null
    Pump 500
    Click $win (Find-Item $win $T_ALLOW $CT::Button) | Out-Null
    Pump 1000
    # the message box is a separate top-level window of our pid: dismiss it
    $all = [System.Windows.Automation.AutomationElement]::RootElement.FindAll(
        $TC::Children, (New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)))
    Say ('top-level windows after clicking 加入白名单: ' + $all.Count)
    $okBtn = $null
    for ($i = 0; $i -lt $all.Count; $i++) {
        foreach ($b in ($all[$i].FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $CT::Button))))) {
            if ($b.Current.Name) { Say ('  dlg btn: ' + $b.Current.Name) }
            if ($b.Current.Name -eq (Ch @(0x786E, 0x5B9A)) -or $b.Current.Name -eq 'OK' -or $b.Current.Name -eq (Ch @(0x662F))) { $okBtn = $b }
        }
    }
    if ($okBtn) { Say 'clicking the message-box OK'; Click $win $okBtn | Out-Null; Pump 1000 }
    Say 'waiting 10s: whitelisted window must survive'
    Pump 10000
    Say ('whitelisted window still alive = ' + [W32]::IsWindow($h2))
} else {
    Say 'ABORT: no hit row found to whitelist'
}
Dump-Page $win 'AFTER-WHITELIST'
Shot $win 'D:\project\cpp\ltool\build\verify\ads_whitelist.png'
Flush

# --- dry run dialog ---
Click $win (Find-Item $win $T_DRY $CT::Button) | Out-Null
Pump 2000
$all2 = [System.Windows.Automation.AutomationElement]::RootElement.FindAll(
    $TC::Children, (New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)))
Say ('top-level windows after 试跑: ' + $all2.Count)
for ($i = 0; $i -lt $all2.Count; $i++) {
    Say ('  win [' + $all2[$i].Current.Name + ']')
    foreach ($t in ($all2[$i].FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $CT::Text))))) {
        if ($t.Current.Name) { foreach ($line in ($t.Current.Name -split "`n")) { Say ('    txt: ' + $line) } }
    }
    foreach ($li in ($all2[$i].FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $CT::ListItem))))) {
        if ($li.Current.Name) { Say ('    item: ' + $li.Current.Name) }
    }
}
$r = $all2[0].Current.BoundingRectangle
if ($r.Width -gt 100) {
    $bmp = New-Object System.Drawing.Bitmap([int]$r.Width, [int]$r.Height)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen([int]$r.X, [int]$r.Y, 0, 0, $bmp.Size)
    $g.Dispose(); $bmp.Save('D:\project\cpp\ltool\build\verify\ads_dryrun.png', [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
}
Flush

# --- cleanup: close our fake windows, restore auto to OFF ---
foreach ($f in @($ad1, $ad2)) { if ($f) { $f.Close() } }
Pump 500
$cb2 = $null
foreach ($c in (Get-Kids $win $CT::CheckBox)) { if ($c.Current.Name.StartsWith($T_AUTO)) { $cb2 = $c } }
if ($cb2) {
    Click $win $cb2 | Out-Null     # we switched it on for the test; switch it back off
    Pump 500
    Say 'auto intercept restored to OFF'
}
Say 'cleanup done'
Flush
Write-Host 'done'
