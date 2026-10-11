$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win32 {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
}
"@
function Ch([int[]]$c) { $s = ''; foreach ($x in $c) { $s += [string][char]$x }; return $s }
$S_SOFT = Ch @(0x8F6F, 0x4EF6)                       # software menu entry
$S_VER  = Ch @(0x7248, 0x672C)                       # version button
$S_SITE = Ch @(0x5B98, 0x7F51)                       # official-site button
$S_SRCH = Ch @(0x641C, 0x7D22)                       # search button
$S_CACHE = Ch @(0x7F13, 0x5B58, 0x4E8E)              # "cached at"
$S_DL   = Ch @(0x4E0B, 0x8F7D)                       # download
$S_UP   = Ch @(0x91CD, 0x65B0, 0x67E5, 0x8BE2)       # re-query

$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$log = [System.Collections.Generic.List[string]]::new()
function Say([string]$s) { $log.Add($s); Write-Host ('| ' + $s) }
function Flush { [System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\soft_report.txt', ($log -join "`r`n"), [System.Text.Encoding]::UTF8) }
function Get-Kids([object]$w, [object]$t) {
    return $w.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $t)))
}
function Invoke-Raise([object]$w) {
    for ($i = 0; $i -lt 8; $i++) {
        [Win32]::keybd_event(0x12, 0, 0, [IntPtr]::Zero)
        [Win32]::keybd_event(0x12, 0, 2, [IntPtr]::Zero)
        [Win32]::SetForegroundWindow([IntPtr]$w.Current.NativeWindowHandle) | Out-Null
        Start-Sleep -Milliseconds 300
        if ([Win32]::GetForegroundWindow() -eq [IntPtr]$w.Current.NativeWindowHandle) { return $true }
    }
    return $false
}
function Invoke-Click([object]$w, [object]$el) {
    if (-not (Invoke-Raise $w)) { throw 'foreground not acquired' }
    $r = $el.Current.BoundingRectangle
    [Win32]::SetCursorPos([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2)) | Out-Null
    Start-Sleep -Milliseconds 200
    [Win32]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 60
    [Win32]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 500
}

if (Get-Process ltool -ErrorAction SilentlyContinue) { throw 'another ltool running' }
$proc = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -ArgumentList '--settings' -PassThru
Say ('pid=' + $proc.Id)
$win = $null
for ($i = 0; $i -lt 25; $i++) {
    Start-Sleep -Milliseconds 800
    if ($proc.HasExited) { throw 'exited early' }
    $cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)
    $win = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst($TC::Children, $cond)
    if ($win) { break }
}
if (-not $win) { throw 'no window' }
Start-Sleep -Seconds 2

$soft = $null
foreach ($it in (Get-Kids $win $CT::ListItem)) { if ($it.Current.Name -eq $S_SOFT) { $soft = $it } }
if (-not $soft) { throw 'software page entry missing' }
Invoke-Click $win $soft
Start-Sleep -Seconds 4

Say '--- buttons on software page (name | tooltip) ---'
$verBtns = @()
foreach ($b in (Get-Kids $win $CT::Button)) {
    $nm = $b.Current.Name
    if ($nm -eq $S_VER -or $nm -eq $S_SITE -or $nm -eq $S_SRCH -or $nm -eq $S_DL) {
        Say ('  [' + $nm + ']  tip=' + $b.Current.HelpText)
    }
    if ($nm -eq $S_VER) { $verBtns += $b }
}
Flush

# open the first row's version menu (7-Zip is row 0) and look for winget spawn
Say ('--- clicking first 版本 button (count=' + $verBtns.Count + ') ---')
Invoke-Click $win $verBtns[0]
$wingetSeen = 0
for ($i = 0; $i -lt 8; $i++) {
    Start-Sleep -Milliseconds 500
    if (Get-Process winget -ErrorAction SilentlyContinue) { $wingetSeen++ }
}
Say ('winget process observed while menu open: ' + $wingetSeen + ' sample(s)')

$popups = [System.Windows.Automation.AutomationElement]::RootElement.FindAll(
    $TC::Children, (New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)))
Say ('top-level windows of pid: ' + $popups.Count)
for ($i = 0; $i -lt $popups.Count; $i++) {
    Say ('  win[' + $i + '] ' + $popups[$i].Current.ControlType.ProgrammaticName + ' [' + $popups[$i].Current.Name + ']')
    foreach ($t in (Get-Kids $popups[$i] $CT::Text)) {
        if ($t.Current.Name) { Say ('    text: ' + $t.Current.Name) }
    }
    foreach ($m in (Get-Kids $popups[$i] $CT::MenuItem)) {
        if ($m.Current.Name) { Say ('    item: ' + $m.Current.Name) }
    }
}
$r = $win.Current.BoundingRectangle
$bmp = New-Object System.Drawing.Bitmap([int]$r.Width, [int]$r.Height)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen([int]$r.X, [int]$r.Y, 0, 0, $bmp.Size)
$g.Dispose(); $bmp.Save('D:\project\cpp\ltool\build\verify\soft_page.png', [System.Drawing.Imaging.ImageFormat]::Png)
Flush
Write-Host 'done'
