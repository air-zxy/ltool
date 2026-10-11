$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class W32 {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out PT p);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
  [StructLayout(LayoutKind.Sequential)] public struct PT { public int X; public int Y; }
}
"@
function Ch([int[]]$c) { $s = ''; foreach ($x in $c) { $s += [string][char]$x }; return $s }
$S_SOFT = Ch @(0x8F6F, 0x4EF6)                        # software page nav
$S_DL   = Ch @(0x4E0B, 0x8F7D)                        # download button
$S_LOGT = Ch @(0x9759, 0x9ED8, 0x5B89, 0x88C5, 0x65E5, 0x5FD7)   # silent install log
$PID_TGT = 0
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$root = [System.Windows.Automation.AutomationElement]::RootElement
$log = [System.Collections.Generic.List[string]]::new()
function Say([string]$s) { $log.Add($s); Write-Host ('| ' + $s) }
function Flush { [System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\seven.txt', ($log -join "`r`n"), [System.Text.Encoding]::UTF8) }
function Kids([object]$w, [object]$t) { return $w.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $t))) }
function Foreground([object]$w) {
    for ($i = 0; $i -lt 10; $i++) {
        [W32]::keybd_event(0x12, 0, 0, [IntPtr]::Zero)
        [W32]::keybd_event(0x12, 0, 2, [IntPtr]::Zero)
        [W32]::SetForegroundWindow([IntPtr]$w.Current.NativeWindowHandle) | Out-Null
        Start-Sleep -Milliseconds 300
        if ([W32]::GetForegroundWindow() -eq [IntPtr]$w.Current.NativeWindowHandle) { return $true }
    }
    return $false
}
function Click([object]$el) {
    $r = $el.Current.BoundingRectangle
    $prev = New-Object W32+PT
    [void][W32]::GetCursorPos([ref]$prev)
    [W32]::SetCursorPos([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2)) | Out-Null
    Start-Sleep -Milliseconds 150
    [W32]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 60
    [W32]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 500
    [W32]::SetCursorPos($prev.X, $prev.Y) | Out-Null
    Say ('  clicked [' + $el.Current.Name + ']')
}
function Tops() { return $root.FindAll($TC::Children, (New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $PID_TGT))) }
function LogWin([object]$settings) {
    foreach ($w in (Tops)) { if ($w.Current.Name.Contains($S_LOGT)) { return $w } }
    foreach ($w in (Kids $settings $CT::Window)) { if ($w.Current.Name.Contains($S_LOGT)) { return $w } }
    $all = $root.FindAll($TC::Children, [System.Windows.Automation.Condition]::TrueCondition)
    foreach ($w in $all) { if ($w.Current.Name.Contains($S_LOGT)) { return $w } }
    return $null
}
function Shoot([object]$w, [string]$name) {
    $r = $w.Current.BoundingRectangle
    $bmp = New-Object System.Drawing.Bitmap([int]$r.Width, [int]$r.Height)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen([int]$r.X, [int]$r.Y, 0, 0, $bmp.Size)
    $g.Dispose(); $bmp.Save('D:\project\cpp\ltool\build\verify\' + $name, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    Say ('  saved ' + $name)
}

$old = Get-Process ltool -ErrorAction SilentlyContinue
if ($old) { Say ('closing old pid ' + ($old.Id -join ',')); Stop-Process -Id $old.Id -Force; Start-Sleep -Seconds 2 }
$proc = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -ArgumentList '--settings' -PassThru
$PID_TGT = $proc.Id
Say ('pid=' + $PID_TGT)
$win = $null
for ($i = 0; $i -lt 25; $i++) {
    Start-Sleep -Milliseconds 600
    if ($proc.HasExited) { throw 'exited early' }
    $win = $root.FindFirst($TC::Children, (New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $PID_TGT)))
    if ($win) { break }
}
if (-not $win) { throw 'no settings window' }
Say ('foreground=' + (Foreground $win))
foreach ($it in (Kids $win $CT::ListItem)) { if ($it.Current.Name -eq $S_SOFT) { Click $it; break } }
Start-Sleep -Seconds 3
[void](Foreground $win)

$zip = $null
foreach ($t in (Kids $win $CT::Text)) { if ($t.Current.Name.Contains('7-Zip')) { $zip = $t; break } }
if (-not $zip) { Say 'no 7-Zip row text'; Shoot $win 'seven_norow.png'; Flush; exit 1 }
$zy = $zip.Current.BoundingRectangle.Y + $zip.Current.BoundingRectangle.Height / 2
$pick = $null; $best = 1e9
foreach ($b in (Kids $win $CT::Button)) {
    if ($b.Current.Name -ne $S_DL) { continue }
    $d = [Math]::Abs($b.Current.BoundingRectangle.Y + $b.Current.BoundingRectangle.Height / 2 - $zy)
    if ($d -lt $best) { $best = $d; $pick = $b }
}
if (-not $pick) { Say 'no download button near 7-Zip'; Flush; exit 1 }
Say ('row distance = ' + [int]$best)
Click $pick

$lw = $null
for ($i = 0; $i -lt 15; $i++) {
    Start-Sleep -Milliseconds 700
    if ($proc.HasExited) { throw ('CRASHED at t+' + $i) }
    $lw = LogWin $win
    if ($lw) { break }
}
if (-not $lw) { Say 'log window never appeared'; Shoot $win 'seven_nolog.png'; Flush; exit 2 }
Start-Sleep -Milliseconds 800
$fh = [W32]::GetForegroundWindow()
Say ('log hwnd=' + $lw.Current.NativeWindowHandle + ' foreground=' + $fh + ' onTop=' + ($fh -eq [IntPtr]$lw.Current.NativeWindowHandle))

function Head() {
    foreach ($t in (Kids $lw $CT::Text)) { if ($t.Current.Name.Contains('7zip')) { return $t.Current.Name } }
    return ''
}
function HeadOf([object]$w) {
    foreach ($t in (Kids $w $CT::Text)) { if ($t.Current.Name.Contains('7zip')) { return $t.Current.Name } }
    return ''
}
$running = Ch @(0x8FDB, 0x884C, 0x4E2D)     # "in progress" marker in the head label
Say ('head@start=[' + (HeadOf $lw) + ']')
for ($i = 0; $i -lt 12; $i++) {
    Start-Sleep -Seconds 3
    if ($proc.HasExited) { throw ('CRASHED t+' + (($i + 1) * 3)) }
    $cur = LogWin $win                       # re-acquire: cached UIA elements go stale
    if (-not $cur) { continue }
    $st = HeadOf $cur
    if ($st -and -not $st.Contains($running)) { $lw = $cur; Say ('finished at t+' + (($i + 1) * 3) + 's'); break }
}
Say ('head@end=[' + (HeadOf $lw) + ']')
[W32]::SetForegroundWindow([IntPtr]$lw.Current.NativeWindowHandle) | Out-Null
Start-Sleep -Milliseconds 600
Shoot $lw 'seven_log.png'
foreach ($li in (Kids $lw $CT::ListItem)) { Say ('JOB: ' + $li.Current.Name) }
if (Test-Path 'D:\software\7-Zip\7z.exe') {
    Say ('7z.exe installed: ' + (Get-Item 'D:\software\7-Zip\7z.exe').LastWriteTime)
} else { Say 'D:\software\7-Zip\7z.exe NOT FOUND' }
Flush
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; Start-Sleep -Seconds 2 }
Start-Process -FilePath 'D:\software\ltool\ltool.exe' | Out-Null
Start-Sleep -Seconds 3
Say ('relaunched installed pid=' + ((Get-Process ltool -ErrorAction SilentlyContinue).Id -join ','))
Flush
Write-Host 'done'
