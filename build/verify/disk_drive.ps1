$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class W32d {
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
$T_DISK   = Ch @(0x78C1, 0x76D8, 0x6E05, 0x7406)              # menu: disk cleanup
$T_OLD    = Ch @(0x0043, 0x76D8, 0x6E05, 0x7406)              # old name "C盘清理"
$T_TIDY   = Ch @(0x6574, 0x7406)                              # the 整理 button
$T_USED   = Ch @(0x76D8, 0x5DF2, 0x7528)                      # "盘已用" title fragment
$T_CLEAN  = Ch @(0x53EF, 0x6E05, 0x7406, 0x5408, 0x8BA1)      # cleanable total
$T_SW     = Ch @(0x5DF2, 0x5207, 0x5230)                      # "已切到" (switched-to notice)

$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$log = [System.Collections.Generic.List[string]]::new()
function Say([string]$s) { $log.Add($s); Write-Host ('| ' + $s) }
function Flush { [System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\disk_drive.txt', ($log -join "`n"), (New-Object System.Text.UTF8Encoding($false))) }
function Get-Kids([object]$w, [object]$t) {
    return $w.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $t)))
}
function Raise([object]$w) {
    for ($i = 0; $i -lt 8; $i++) {
        [W32d]::keybd_event(0x12, 0, 0, [IntPtr]::Zero)
        [W32d]::keybd_event(0x12, 0, 2, [IntPtr]::Zero)
        [W32d]::SetForegroundWindow([IntPtr]$w.Current.NativeWindowHandle) | Out-Null
        Start-Sleep -Milliseconds 300
        if ([W32d]::GetForegroundWindow() -eq [IntPtr]$w.Current.NativeWindowHandle) { return $true }
    }
    return $false
}
function Click([object]$w, [object]$el) {
    if (-not (Raise $w)) { Say 'SKIP CLICK: foreground not acquired'; return $false }
    $r = $el.Current.BoundingRectangle
    $pt = New-Object System.Drawing.Point ([int]($r.X + $r.Width / 2)), ([int]($r.Y + $r.Height / 2))
    $hit = [W32d]::GetAncestor([W32d]::GetWindowFromPoint($pt), 2)
    if ($hit -ne [IntPtr]$w.Current.NativeWindowHandle) { Say 'SKIP CLICK: point is on another window'; return $false }
    [W32d]::SetCursorPos($pt.X, $pt.Y) | Out-Null
    Start-Sleep -Milliseconds 150
    [W32d]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 60
    [W32d]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 500
    return $true
}
function Texts([object]$w) {
    $out = @()
    foreach ($t in (Get-Kids $w $CT::Text)) { if ($t.Current.Name) { $out += $t.Current.Name } }
    return $out
}
function Shot([object]$w, [string]$file) {
    $r = $w.Current.BoundingRectangle
    $bmp = New-Object System.Drawing.Bitmap([int]$r.Width, [int]$r.Height)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen([int]$r.X, [int]$r.Y, 0, 0, $bmp.Size)
    $g.Dispose(); $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
}

if (Get-Process ltool -ErrorAction SilentlyContinue) { Say 'ABORT: another ltool running'; Flush; exit 1 }
$proc = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -ArgumentList '--settings' -PassThru
Say ('pid=' + $proc.Id)
$win = $null
for ($i = 0; $i -lt 25; $i++) {
    Start-Sleep -Milliseconds 800
    $win = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst(
        $TC::Children, (New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)))
    if ($win) { break }
}
if (-not $win) { Say 'ABORT: no window'; Flush; exit 1 }
Start-Sleep -Seconds 2

# --- menu renamed? ---
$names = @()
foreach ($li in (Get-Kids $win $CT::ListItem)) { $names += $li.Current.Name }
Say ('menu entries: ' + ($names -join ' / '))
Say ('has new name 磁盘清理 = ' + ($names -contains $T_DISK) + '   still has C盘清理 = ' + ($names -contains $T_OLD))
$item = $null
foreach ($li in (Get-Kids $win $CT::ListItem)) { if ($li.Current.Name -eq $T_DISK) { $item = $li } }
if (-not $item) { Say 'ABORT: disk page entry missing'; Flush; exit 1 }

$onDisk = $false
for ($try = 1; $try -le 3 -and -not $onDisk; $try++) {
    Click $win $item | Out-Null
    for ($i = 0; $i -lt 10 -and -not $onDisk; $i++) {
        Start-Sleep -Milliseconds 500
        foreach ($c in (Get-Kids $win $CT::ComboBox)) { if ($c.Current.BoundingRectangle.Width -gt 100) { $onDisk = $true } }
    }
    Say ('disk page attempt ' + $try + ' -> ' + $onDisk)
}
if (-not $onDisk) { Say 'STUCK: never reached the disk page (no combo found)'; Flush; exit 1 }

$combo = $null
foreach ($c in (Get-Kids $win $CT::ComboBox)) { if ($c.Current.BoundingRectangle.Width -gt 100) { $combo = $c } }
Say ('combo value = [' + $combo.Current.Name + '] rect=' + [math]::Round($combo.Current.BoundingRectangle.Width,0))
Say '--- summary texts on the disk page ---'
foreach ($t in (Texts $win)) { if ($t.Length -lt 120) { Say ('  txt: ' + $t) } }
Shot $win 'D:\project\cpp\ltool\build\verify\disk_c.png'
Flush

# --- expand the combo and read the drive list ---
$ec = $null
try { $ec = $combo.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern) } catch { Say ('no expand pattern: ' + $_.Exception.Message) }
if ($ec) {
    $ec.Expand()
    Start-Sleep -Milliseconds 800
    $tops = [System.Windows.Automation.AutomationElement]::RootElement.FindAll(
        $TC::Children, (New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)))
    Say ('windows while expanded = ' + $tops.Count)
    $items = @()
    for ($i = 0; $i -lt $tops.Count; $i++) {
        foreach ($li in ($tops[$i].FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $CT::ListItem))))) {
            if ($li.Current.Name) { $items += $li.Current.Name }
        }
    }
    foreach ($s in $items) { Say ('  drive item: ' + $s) }
    Flush
    # pick the last item (a non-system drive if there is one)
    if ($items.Count -gt 1) {
        $target = $null
        for ($i = 0; $i -lt $tops.Count; $i++) {
            foreach ($li in ($tops[$i].FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $CT::ListItem))))) {
                if ($li.Current.Name -eq $items[$items.Count - 1]) { $target = $li }
            }
        }
        if ($target) {
            Say ('clicking drive: ' + $target.Current.Name)
            Click $win $target | Out-Null
            Start-Sleep -Seconds 2
            Say '--- after switching drives ---'
            foreach ($t in (Texts $win)) { if ($t.Length -lt 120) { Say ('  txt: ' + $t) } }
            Shot $win 'D:\project\cpp\ltool\build\verify\disk_d.png'
            Flush
        }
    } else {
        $ec.Collapse()
    }
}
# --- press 整理 and watch the progress text mention the new root ---
$tidy = $null
foreach ($b in (Get-Kids $win $CT::Button)) { if ($b.Current.Name -eq $T_TIDY) { $tidy = $b } }
if ($tidy) {
    Click $win $tidy | Out-Null
    for ($i = 0; $i -lt 8; $i++) {
        Start-Sleep -Seconds 2
        $line = ''
        foreach ($t in (Texts $win)) { if ($t.Contains((Ch @(0x5df2, 0x5207, 0x5230))) -or $t.Contains((Ch @(0x6b63, 0x5728))) -or $t.Contains('/')) { $line = $t } }
        Say ('  t=' + (($i + 1) * 2) + 's: ' + $line)
    }
    Shot $win 'D:\project\cpp\ltool\build\verify\disk_scan.png'
} else { Say 'no 整理 button found' }
Flush
Write-Host 'done'
