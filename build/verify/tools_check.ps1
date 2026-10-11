$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class W32t {
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
$T_TOOLS  = Ch @(0x5DE5, 0x5177)                                  # 工具
$T_BACK   = Ch @(0x2039, 0x0020, 0x8FD4, 0x56DE)                  # ‹ 返回
$T_DLL    = Ch @(0x0044, 0x004C, 0x004C, 0x0020, 0x4FEE, 0x590D)  # DLL 修复
$T_ADS    = Ch @(0x5E7F, 0x544A, 0x5F39, 0x7A97)                  # 广告弹窗
$T_DISK   = Ch @(0x78C1, 0x76D8, 0x6E05, 0x7406)                  # 磁盘清理
$T_NET    = Ch @(0x7F51, 0x7EDC)                                  # 网络
$T_JSON   = Ch @(0x004A, 0x0053, 0x004F, 0x004E)                  # JSON
$T_B64    = Ch @(0x0042, 0x0061, 0x0073, 0x0065, 0x0036, 0x0034)  # Base64

$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$log = [System.Collections.Generic.List[string]]::new()
function Say([string]$s) { $log.Add($s); Write-Host ('| ' + $s) }
function Flush { [System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\tools_page.txt', ($log -join "`n"), (New-Object System.Text.UTF8Encoding($false))) }
function Kids([object]$w, [object]$t) { return $w.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $t))) }
function Raise([object]$w) {
    for ($i = 0; $i -lt 6; $i++) {
        [W32t]::keybd_event(0x12, 0, 0, [IntPtr]::Zero)
        [W32t]::keybd_event(0x12, 0, 2, [IntPtr]::Zero)
        [W32t]::SetForegroundWindow([IntPtr]$w.Current.NativeWindowHandle) | Out-Null
        Start-Sleep -Milliseconds 300
        if ([W32t]::GetForegroundWindow() -eq [IntPtr]$w.Current.NativeWindowHandle) { return $true }
    }
    return $false
}
function Click([object]$w, [object]$el) {
    if (-not (Raise $w)) { Say '  SKIP CLICK: foreground not acquired'; return $false }
    $r = $el.Current.BoundingRectangle
    $pt = New-Object System.Drawing.Point ([int]($r.X + $r.Width / 2)), ([int]($r.Y + $r.Height / 2))
    if ([W32t]::GetAncestor([W32t]::GetWindowFromPoint($pt), 2) -ne [IntPtr]$w.Current.NativeWindowHandle) {
        Say '  SKIP CLICK: point belongs to another window'; return $false }
    [W32t]::SetCursorPos($pt.X, $pt.Y) | Out-Null
    Start-Sleep -Milliseconds 150
    [W32t]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 60
    [W32t]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 600
    return $true
}
function Shot([object]$w, [string]$file) {
    $r = $w.Current.BoundingRectangle
    $bmp = New-Object System.Drawing.Bitmap([int]$r.Width, [int]$r.Height)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen([int]$r.X, [int]$r.Y, 0, 0, $bmp.Size)
    $g.Dispose(); $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
}
function VisibleTexts([object]$w) {
    foreach ($t in (Kids $w $CT::Text)) {
        if ($t.Current.Name -and -not $t.Current.IsOffscreen) { Say ('  txt: ' + $t.Current.Name) }
    }
}

if (Get-Process ltool -ErrorAction SilentlyContinue) { Say 'ABORT: another ltool running'; Flush; exit 1 }
$proc = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -ArgumentList '--settings' -PassThru
$win = $null
for ($i = 0; $i -lt 25; $i++) {
    Start-Sleep -Milliseconds 800
    $win = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst(
        $TC::Children, (New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)))
    if ($win) { break }
}
if (-not $win) { Say 'ABORT: no window'; Flush; exit 1 }
Start-Sleep -Seconds 2

Say '--- menu entries (visible list items) ---'
$menu = @()
foreach ($li in (Kids $win $CT::ListItem)) {
    if (-not $li.Current.IsOffscreen -and $li.Current.BoundingRectangle.X -lt 800) { $menu += $li.Current.Name }
}
Say ('  ' + ($menu -join ' / '))
Say ('  has 工具 = ' + ($menu -contains $T_TOOLS))
foreach ($gone in @($T_ADS, $T_DISK, $T_NET)) {
    Say ('  old top-level entry "' + $gone + '" still present = ' + ($menu -contains $gone))
}
Flush

$item = $null
foreach ($li in (Kids $win $CT::ListItem)) { if ($li.Current.Name -eq $T_TOOLS) { $item = $li } }
if (-not $item) { Say 'ABORT: no 工具 entry'; Flush; exit 1 }
$ok = Click $win $item
Say ('clicked 工具 = ' + $ok)
if ($ok) {
    Start-Sleep -Seconds 2
    Say '--- after opening 工具 ---'
    VisibleTexts $win
    Shot $win 'D:\project\cpp\ltool\build\verify\tools_grid.png'
    # 点第一张卡片（DLL 修复）看返回条和内嵌页
    $card = $null
    foreach ($t in (Kids $win $CT::Text)) {
        if ($t.Current.Name -eq $T_DLL -and -not $t.Current.IsOffscreen) { $card = $t }
    }
    if ($card) {
        Say 'clicking the DLL 修复 card'
        if (Click $win $card) {
            Start-Sleep -Seconds 3
            Say '--- inside the embedded page ---'
            VisibleTexts $win
            Shot $win 'D:\project\cpp\ltool\build\verify\tools_embedded.png'
            $back = $null
            foreach ($b in (Kids $win $CT::Button)) { if ($b.Current.Name.StartsWith($T_BACK)) { $back = $b } }
            Say ('  back button found = ' + ($null -ne $back))
            if ($back) { Click $win $back | Out-Null; Start-Sleep -Seconds 1; Say '--- after 返回 ---'; VisibleTexts $win }
        }
    } else { Say '  no DLL 修复 card text found' }
}
Flush
Stop-Process -Name ltool -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1
Start-Process -FilePath 'D:\software\ltool\ltool.exe'
Say 'stopped the test instance, restarted the installed one'
Flush
Write-Host 'done'
