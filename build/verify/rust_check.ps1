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
$S_LANG    = Ch @(0x8BED, 0x8A00)              # yuyan  = language page
$S_LOCAL   = Ch @(0x5DF2, 0x88C5)              # yizhuang = installed
$S_REMOT   = Ch @(0x53EF, 0x5B89, 0x88C5)      # keanzhuang = installable
$S_PAREN   = Ch @(0xFF08)                      # fullwidth (
$PID_TGT = 0
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$root = [System.Windows.Automation.AutomationElement]::RootElement
$log = [System.Collections.Generic.List[string]]::new()
function Say([string]$s) { $log.Add($s); Write-Host ('| ' + $s) }
function Flush { [System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\rust.txt', ($log -join "`r`n"), [System.Text.Encoding]::UTF8) }
function Kids([object]$w, [object]$t) {
    if ($null -eq $w) { return $root.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $PID_TGT))) }
    return $w.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $t)))
}
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
    return $true
}
function Shoot([string]$name) {
    $cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $PID_TGT)
    $w = $root.FindFirst($TC::Children, $cond)
    if (-not $w) { Say '  no window to shoot'; return }
    $r = $w.Current.BoundingRectangle
    $bmp = New-Object System.Drawing.Bitmap([int]$r.Width, [int]$r.Height)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen([int]$r.X, [int]$r.Y, 0, 0, $bmp.Size)
    $g.Dispose(); $bmp.Save('D:\project\cpp\ltool\build\verify\' + $name, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    Say ('  saved ' + $name)
}

$old = Get-Process ltool -ErrorAction SilentlyContinue
if ($old) { Say ('closing old pid ' + ($old.Id -join ',')); Stop-Process -Id $old.Id -Force; Start-Sleep -Seconds 2 }
$proc = Start-Process -FilePath 'D:\software\ltool\ltool.exe' -ArgumentList '--settings' -PassThru
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

foreach ($it in (Kids $win $CT::ListItem)) { if ($it.Current.Name -eq $S_LANG) { Click $it | Out-Null; break } }
Start-Sleep -Seconds 2
[void](Foreground $win)
$rust = $null
foreach ($it in (Kids $win $CT::ListItem)) { if ($it.Current.Name.ToLower().Contains('rust')) { $rust = $it; break } }
if (-not $rust) { Say 'rust language entry missing'; Shoot 'rust_nolang.png'; Flush; exit 1 }
Click $rust | Out-Null
Start-Sleep -Seconds 2
foreach ($t in (Kids $win $CT::Text)) {
    $n = $t.Current.Name
    if ($n.Contains('Rustlang') -or $n.Contains('Rustup') -or $n.Contains('MSVC')) { Say ('GROUP: ' + $n) }
}
Shoot 'rust_page.png'

$cb = Kids $win $CT::ComboBox
Say ('comboboxes=' + $cb.Count)
if ($cb.Count -gt 0) {
    $pat = $cb[0].GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern)
    $pat.Expand()
    Start-Sleep -Milliseconds 900
    $seen = 0
    foreach ($li in (Kids $null $CT::ListItem)) {
        $n = $li.Current.Name
        if ($n.Contains($S_LOCAL) -or $n.Contains($S_REMOT) -or $n.Contains($S_PAREN)) {
            Say ('DROPDOWN: ' + $n); $seen++
        }
    }
    if ($seen -eq 0) { Say 'no dropdown items matched' }
    Shoot 'rust_dropdown.png'
    $pat.Collapse()
}
Flush
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; Start-Sleep -Seconds 2 }
Start-Process -FilePath 'D:\software\ltool\ltool.exe' | Out-Null
Start-Sleep -Seconds 3
Say ('relaunched installed instance pid=' + ((Get-Process ltool -ErrorAction SilentlyContinue).Id -join ','))
Flush
Write-Host 'done'
