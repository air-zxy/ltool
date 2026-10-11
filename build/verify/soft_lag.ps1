param([string]$Exe = 'D:\project\cpp\ltool\build\Release\ltool.exe')
$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class W32e {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
  [DllImport("user32.dll")] public static extern IntPtr GetWindowFromPoint(POINT p);
  [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h, uint flags);
}
"@
function Ch([int[]]$c) { $s = ''; foreach ($x in $c) { $s += [string][char]$x }; return $s }
$T_SOFT  = Ch @(0x8F6F, 0x4EF6)                       # software menu entry
$T_CHECK = [string][char]0x2714                      # the installed marker in the status column

$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$log = [System.Collections.Generic.List[string]]::new()
function Say([string]$s) { $log.Add($s); Write-Host ('| ' + $s) }
function Flush { [System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\soft_lag.txt', ($log -join "`n"), (New-Object System.Text.UTF8Encoding($false))) }
function Get-Kids([object]$w, [object]$t) {
    return $w.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $t)))
}
function Visible([object]$e) { return (-not $e.Current.IsOffscreen) }
function Raise([object]$w) {
    for ($i = 0; $i -lt 10; $i++) {
        [W32e]::keybd_event(0x12, 0, 0, [IntPtr]::Zero)
        [W32e]::keybd_event(0x12, 0, 2, [IntPtr]::Zero)
        [W32e]::SetForegroundWindow([IntPtr]$w.Current.NativeWindowHandle) | Out-Null
        Start-Sleep -Milliseconds 300
        if ([W32e]::GetForegroundWindow() -eq [IntPtr]$w.Current.NativeWindowHandle) { return $true }
    }
    return $false
}
function Click([object]$w, [object]$el) {
    if (-not (Raise $w)) { Say 'SKIP CLICK: foreground not acquired'; return $false }
    $r = $el.Current.BoundingRectangle
    $pt = New-Object System.Drawing.Point ([int]($r.X + $r.Width / 2)), ([int]($r.Y + $r.Height / 2))
    if ([W32e]::GetAncestor([W32e]::GetWindowFromPoint($pt), 2) -ne [IntPtr]$w.Current.NativeWindowHandle) {
        Say 'SKIP CLICK: point is on another window'; return $false
    }
    [W32e]::SetCursorPos($pt.X, $pt.Y) | Out-Null
    Start-Sleep -Milliseconds 150
    [W32e]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 60
    [W32e]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
    return $true
}
function CountVersions([object]$w) {
    $n = 0
    foreach ($t in (Get-Kids $w $CT::Text)) {
        if ((Visible $t) -and $t.Current.Name.StartsWith($T_CHECK) -and $t.Current.Name.Contains('v')) { $n++ }
    }
    return $n
}

Say ('exe = ' + $Exe)
if (Get-Process ltool -ErrorAction SilentlyContinue) { Say 'ABORT: another ltool running'; Flush; exit 1 }
$proc = Start-Process -FilePath $Exe -ArgumentList '--settings' -PassThru
$win = $null
for ($i = 0; $i -lt 30; $i++) {
    Start-Sleep -Milliseconds 800
    $win = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst(
        $TC::Children, (New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)))
    if ($win) { break }
}
if (-not $win) { Say 'ABORT: no settings window'; Flush; exit 1 }
Start-Sleep -Seconds 3

$item = $null
foreach ($li in (Get-Kids $win $CT::ListItem)) { if ((Visible $li) -and $li.Current.Name -eq $T_SOFT) { $item = $li } }
if (-not $item) { Say 'ABORT: software menu entry not visible'; Flush; exit 1 }
if (-not (Click $win $item)) { Flush; exit 1 }

# UIA 调用由目标进程的 GUI 线程应答：点完立刻读一次，读到的耗时≈界面被占住的时间
$sw = [Diagnostics.Stopwatch]::StartNew()
$null = $win.Current.Name
$sw.Stop()
$freeze = [math]::Round($sw.Elapsed.TotalMilliseconds)
Say ('GUI blocked for about ' + $freeze + 'ms right after the click')

$seen = @()
for ($i = 0; $i -lt 24; $i++) {
    Start-Sleep -Milliseconds 500
    $seen += (CountVersions $win)
}
Say ('version cells over time (every 0.5s): ' + ($seen -join ', '))
Say ('final = ' + $seen[$seen.Count - 1] + ' rows showing a version')
$r = $win.Current.BoundingRectangle
$bmp = New-Object System.Drawing.Bitmap([int]$r.Width, [int]$r.Height)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen([int]$r.X, [int]$r.Y, 0, 0, $bmp.Size)
$g.Dispose(); $bmp.Save('D:\project\cpp\ltool\build\verify\soft_lag.png', [System.Drawing.Imaging.ImageFormat]::Png)
Flush
Write-Host 'done'
