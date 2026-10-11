$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class W32 {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
  [DllImport("kernel32.dll")] public static extern uint GetTickCount();
}
"@
function Ch([int[]]$c) { $s = ''; foreach ($x in $c) { $s += [string][char]$x }; return $s }
$S_DISK = Ch @(0x43, 0x76D8, 0x6E05, 0x7406)      # C盘清理 nav
$S_ORG  = Ch @(0x6574, 0x7406)                    # 整理
$S_TAB2 = Ch @(0x5360, 0x7528, 0x5206, 0x6790)    # 占用分析 tab
$S_SCAN = Ch @(0x626B, 0x63CF, 0x4E2D)            # "scanning" marker
$S_LIST = Ch @(0x672C, 0x5C42, 0x76EE, 0x5F55)   # "this level dirs" list header
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$root = [System.Windows.Automation.AutomationElement]::RootElement
function Say([string]$s) { Write-Host ('| ' + $s) }
function Kids([object]$w, [object]$t) { return $w.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $t))) }
function Foreground([object]$w) {
    for ($i = 0; $i -lt 8; $i++) {
        [W32]::keybd_event(0x12, 0, 0, [IntPtr]::Zero); [W32]::keybd_event(0x12, 0, 2, [IntPtr]::Zero)
        [W32]::SetForegroundWindow([IntPtr]$w.Current.NativeWindowHandle) | Out-Null
        Start-Sleep -Milliseconds 250
        if ([W32]::GetForegroundWindow() -eq [IntPtr]$w.Current.NativeWindowHandle) { return $true }
    }
    return $false
}
function At([object]$w, [object]$el) {
    $r = $el.Current.BoundingRectangle
    [W32]::SetCursorPos([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2)) | Out-Null
    Start-Sleep -Milliseconds 120
    [W32]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 60
    [W32]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 300
    Say ('  clicked [' + $el.Current.Name + ']')
}
function Shoot([object]$w, [string]$name) {
    $r = $w.Current.BoundingRectangle
    $bmp = New-Object System.Drawing.Bitmap([int]$r.Width, [int]$r.Height)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen([int]$r.X, [int]$r.Y, 0, 0, $bmp.Size)
    $g.Dispose(); $bmp.Save('D:\project\cpp\ltool\build\verify\' + $name, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    Say ('  saved ' + $name)
}
function Statuses([object]$w) {
    $r = @()
    foreach ($t in (Kids $w $CT::Text)) { $n = $t.Current.Name; if ($n.Length -gt 3) { $r += $n } }
    return $r
}

$old = Get-Process ltool -ErrorAction SilentlyContinue
if ($old) { Stop-Process -Id $old.Id -Force; Start-Sleep -Seconds 2 }
$proc = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -ArgumentList '--settings' -PassThru
$win = $null
for ($i = 0; $i -lt 25; $i++) {
    Start-Sleep -Milliseconds 600
    if ($proc.HasExited) { throw 'exited early' }
    $win = $root.FindFirst($TC::Children, (New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)))
    if ($win) { break }
}
if (-not $win) { throw 'no settings window' }
if (-not (Foreground $win)) { Say 'FAIL: no foreground, no clicks sent'; exit 3 }
foreach ($it in (Kids $win $CT::ListItem)) { if ($it.Current.Name -eq $S_DISK) { At $win $it; break } }
Start-Sleep -Seconds 2
Shoot $win 'disk_tab1_before.png'
$org = $null
foreach ($b in (Kids $win $CT::Button)) { if ($b.Current.Name -eq $S_ORG) { $org = $b; break } }
if (-not $org) { Say 'FAIL: 整理 button missing'; exit 4 }
$t0 = [W32]::GetTickCount()
At $win $org
Say 'clicked 整理, waiting for the tree scan…'
$scanned = $false
for ($i = 0; $i -lt 70; $i++) {
    Start-Sleep -Seconds 3
    if ($proc.HasExited) { throw ('CRASHED t+' + (($i + 1) * 3)) }
    $st = (Statuses $win) | Where-Object { $_.Contains($S_SCAN) }
    if (-not $st) { $scanned = $true; Say ('scan finished in ~' + (([W32]::GetTickCount() - $t0) / 1000) + 's'); break }
}
if (-not $scanned) { Say 'FAIL: scan still running after 120s' }
foreach ($it in (Kids $win $CT::TabItem)) { if ($it.Current.Name -eq $S_TAB2) { At $win $it; break } }
Start-Sleep -Seconds 2
Shoot $win 'disk_tab2_root.png'
foreach ($t in (Statuses $win)) { Say ('TEXT: ' + ($t -replace "`r?`n", ' / ')) }

# 下钻：点列表里最大那个目录的一行（列表按大小降序，第一行就是它）
$head = $null
foreach ($t in (Kids $win $CT::Text)) { if ($t.Current.Name -eq $S_LIST) { $head = $t; break } }
$row = $null
if ($head) {
    $hy = $head.Current.BoundingRectangle.Y
    foreach ($t in (Kids $win $CT::Text)) {
        $r = $t.Current.BoundingRectangle
        if ($r.Y -gt ($hy + 8) -and $r.Height -gt 8 -and $r.Height -lt 30 -and $t.Current.Name.Length -gt 1 -and $t.Current.Name.Length -lt 40) {
            $row = $t; break
        }
    }
}
if (-not $row) { Say 'FAIL: no drill row found' } else {
    $t1 = [W32]::GetTickCount()
    At $win $row
    $dt = ([W32]::GetTickCount() - $t1)
    Say ('drill click took ' + $dt + ' ms (含 UIA 取坐标)')
    Start-Sleep -Seconds 2
    Shoot $win 'disk_tab3_drilled.png'
    foreach ($t in (Statuses $win)) { Say ('AFTER: ' + ($t -replace "`r?`n", ' / ')) }
}
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; Start-Sleep -Seconds 2 }
Start-Process 'D:\software\ltool\ltool.exe' | Out-Null
Start-Sleep -Seconds 3
Say ('relaunched installed pid=' + ((Get-Process ltool -ErrorAction SilentlyContinue).Id -join ','))
