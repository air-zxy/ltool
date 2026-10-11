$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

$cs = @'
using System;
using System.Runtime.InteropServices;
public static class Clicker {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(POINT p);
    [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h, uint kind);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint d, uint e, UIntPtr extra, UIntPtr info);
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int x, y; }
    public const uint GA_ROOT = 2;
    public static void Tap(int x, int y) {
        SetCursorPos(x, y);
        mouse_event(0x0002, 0, 0, UIntPtr.Zero, UIntPtr.Zero);
        mouse_event(0x0004, 0, 0, UIntPtr.Zero, UIntPtr.Zero);
    }
    public static IntPtr RootAt(int x, int y) {
        POINT p; p.x = x; p.y = y;
        return GetAncestor(WindowFromPoint(p), GA_ROOT);
    }
}
'@
Add-Type -TypeDefinition $cs

Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force }
Start-Sleep -Milliseconds 800
$p = Start-Process -FilePath 'D:/project/cpp/ltool/build/Release/ltool.exe' -ArgumentList '--settings' -PassThru
Start-Sleep -Seconds 5

$root = [System.Windows.Automation.AutomationElement]::RootElement
$cond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ProcessIdProperty, $p.Id)
$dlg = $null
foreach ($w in $root.FindAll([System.Windows.Automation.TreeScope]::Children, $cond)) {
    if ($w.Current.ClassName -eq 'SettingsDialog') { $dlg = $w; break }
}
if ($null -eq $dlg) { Write-Output 'FAIL: no SettingsDialog'; exit 1 }
$hwnd = [IntPtr] $dlg.Current.NativeWindowHandle

$ok = $false
for ($i = 0; $i -lt 8; $i++) {
    [Clicker]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)
    [Clicker]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
    [void][Clicker]::SetForegroundWindow($hwnd)
    Start-Sleep -Milliseconds 150
    if ([Clicker]::GetForegroundWindow() -eq $hwnd) { $ok = $true; break }
}
if (-not $ok) { Write-Output 'ABORT: 拿不到前台，不点任何东西'; exit 1 }
Write-Output 'foreground ok'

function Find-ByName([string]$name, [System.Windows.Automation.ControlType]$type) {
    $c = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ControlTypeProperty, $type)
    foreach ($e in $script:dlg.FindAll([System.Windows.Automation.TreeScope]::Descendants, $c)) {
        if ($e.Current.IsOffscreen) { continue }
        if ($e.Current.Name -eq $name) { return $e }
    }
    return $null
}

function Click-Center($e, [string]$what) {
    $r = $e.Current.BoundingRectangle
    $x = [int]($r.X + $r.Width / 2)
    $y = [int]($r.Y + $r.Height / 2)
    if ([Clicker]::RootAt($x, $y) -ne $script:hwnd) {
        Write-Output ("ABORT: $what 的坐标上不是我们的窗口，停手")
        exit 1
    }
    [Clicker]::Tap($x, $y)
    Write-Output ("clicked $what at $x,$y")
    Start-Sleep -Milliseconds 600
}

$tools = Find-ByName ([string][char]0x5DE5 + [char]0x5177) ([System.Windows.Automation.ControlType]::ListItem)
if ($null -eq $tools) { Write-Output 'FAIL: no tools menu row'; exit 1 }
Click-Center $tools 'menu 工具'

$regTxt = Find-ByName ([string][char]0x6CE8 + [char]0x518C + [char]0x8868 + [char]0x5206 + [char]0x6790) `
    ([System.Windows.Automation.ControlType]::Text)
if ($null -eq $regTxt) { Write-Output 'FAIL: no registry card'; exit 1 }
Click-Center $regTxt 'card 注册表分析'

$go = Find-ByName ([string][char]0x5F00 + [char]0x59CB + [char]0x5206 + [char]0x6790) `
    ([System.Windows.Automation.ControlType]::Button)
if ($null -eq $go) { Write-Output 'FAIL: no analyze button'; exit 1 }
$ip = $null
[void]$go.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref] $ip)
$ip.Invoke()
Write-Output 'scan started'
Start-Sleep -Seconds 4

$delName = [string][char]0x5907 + [char]0x4EFD + [char]0x5E76 + [char]0x5220 + [char]0x9664 + [char]0x6240 + [char]0x9009
$del = Find-ByName $delName ([System.Windows.Automation.ControlType]::Button)
if ($null -eq $del) { Write-Output 'FAIL: no delete button'; exit 1 }
Write-Output ('delete enabled before click = ' + $del.Current.IsEnabled)

$listCond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
    [System.Windows.Automation.ControlType]::List)
$target = $null
foreach ($l in $dlg.FindAll([System.Windows.Automation.TreeScope]::Descendants, $listCond)) {
    if ($l.Current.IsOffscreen) { continue }
    foreach ($it in $l.FindAll([System.Windows.Automation.TreeScope]::Children,
            [System.Windows.Automation.Condition]::TrueCondition)) {
        $r = $it.Current.BoundingRectangle
        # 数据行的 item 本身没有文字（文字在子控件里），分组表头有名字
        if ($it.Current.Name -ne '') { continue }
        if ($r.Width -lt 200 -or $r.Height -lt 40) { continue }
        $target = $it
        break
    }
    if ($target) { break }
}
if ($null -eq $target) { Write-Output 'FAIL: no list row'; exit 1 }
$rr = $target.Current.BoundingRectangle
# 勾选框在行的最左边：往右 14px 就是指示器
Click-Center $target 'row body'
$del = Find-ByName $delName ([System.Windows.Automation.ControlType]::Button)
Write-Output ('after clicking row body: selected=' + $target.Current.Selected + ' delete enabled=' + $del.Current.IsEnabled)

# 选中成功的标志：页面底部状态行会打印 "HKEY_...\" 这个键路径
$txtCond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
    [System.Windows.Automation.ControlType]::Text)
$hit = ''
foreach ($t in $dlg.FindAll([System.Windows.Automation.TreeScope]::Descendants, $txtCond)) {
    if ($t.Current.IsOffscreen) { continue }
    if ($t.Current.Name -like '*HKEY_*') { $hit = $t.Current.Name; break }
}
Write-Output ('status shows registry path: ' + ($hit -ne ''))
if ($hit) { Write-Output ('  ' + $hit.Substring(0, [Math]::Min(90, $hit.Length))) }

# 勾选框在行内最左边（UIA 看不见 item 持久控件的子控件，按坐标点）
$cy = [int]($rr.Y + $rr.Height / 2)
foreach ($dx in @(40, 44, 36, 48)) {
    $cx = [int]($rr.X + $dx)
    if ([Clicker]::RootAt($cx, $cy) -ne $hwnd) { Write-Output "ABORT: point not ours"; exit 1 }
    [Clicker]::Tap($cx, $cy)
    Start-Sleep -Milliseconds 400
    $del = Find-ByName $delName ([System.Windows.Automation.ControlType]::Button)
    Write-Output ("tap x+" + $dx + " -> delete enabled=" + $del.Current.IsEnabled)
    if ($del.Current.IsEnabled) { break }
}
