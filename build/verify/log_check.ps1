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
$S_LANG = Ch @(0x8BED, 0x8A00)                        # language page nav
$S_LOGT = Ch @(0x9759, 0x9ED8, 0x5B89, 0x88C5, 0x65E5, 0x5FD7)   # silent install log title
$PID_TGT = 0
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$root = [System.Windows.Automation.AutomationElement]::RootElement
$log = [System.Collections.Generic.List[string]]::new()
function Say([string]$s) { $log.Add($s); Write-Host ('| ' + $s) }
function Flush { [System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\log_check.txt', ($log -join "`r`n"), [System.Text.Encoding]::UTF8) }
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
function WinOfPid() {
    return $root.FindAll($TC::Children, (New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $PID_TGT)))
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
foreach ($it in (Kids $win $CT::ListItem)) { if ($it.Current.Name -eq $S_LANG) { Click $it; break } }
Start-Sleep -Seconds 2
[void](Foreground $win)
foreach ($it in (Kids $win $CT::ListItem)) { if ($it.Current.Name.ToLower().Contains('rust')) { Click $it; break } }
Start-Sleep -Seconds 2
$btn = $null
foreach ($b in (Kids $win $CT::Button)) { if ($b.Current.Name.Contains('winget')) { $btn = $b; break } }
if (-not $btn) { Say 'install button missing'; Flush; exit 1 }
Click $btn
Start-Sleep -Seconds 3

$logw = $null
for ($i = 0; $i -lt 40; $i++) {
    Start-Sleep -Seconds 2
    if ($proc.HasExited) { throw ('CRASHED t+' + ($i * 2)) }
    foreach ($w in (WinOfPid)) { if ($w.Current.Name.Contains($S_LOGT)) { $logw = $w; break } }
    if ($logw) { Say ('log window appeared at t+' + ($i * 2) + 's'); break }
}
if (-not $logw) { Say 'log window never appeared'; Shoot $win 'logcheck_nowin.png'; Flush; exit 2 }
function HeadOf([object]$w) {
    foreach ($t in (Kids $w $CT::Text)) { if ($t.Current.Name.Contains('Rustlang')) { return $t.Current.Name } }
    return ''
}
$init = HeadOf $logw
Say ('head@start=[' + $init + ']')
for ($i = 0; $i -lt 25; $i++) {
    Start-Sleep -Seconds 4
    if ($proc.HasExited) { throw ('CRASHED t+' + (($i + 1) * 4)) }
    $st = HeadOf $logw
    if ($st -ne $init) { Say ('head changed at t+' + (($i + 1) * 4) + 's =[' + $st + ']'); break }
}
Shoot $logw 'logcheck_window.png'
foreach ($t in (Kids $logw $CT::Text)) { Say ('TEXT: ' + $t.Current.Name) }
foreach ($li in (Kids $logw $CT::ListItem)) { Say ('JOB: ' + $li.Current.Name) }
Flush
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; Start-Sleep -Seconds 2 }
Start-Process -FilePath 'D:\software\ltool\ltool.exe' | Out-Null
Start-Sleep -Seconds 3
Say ('relaunched installed pid=' + ((Get-Process ltool -ErrorAction SilentlyContinue).Id -join ','))
Flush
Write-Host 'done'
