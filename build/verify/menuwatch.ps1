$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$Auto = [System.Windows.Automation.AutomationElement]
$src = @'
using System;
using System.Runtime.InteropServices;
public static class W32 {
  [DllImport("user32.dll")] public static extern void SwitchToThisWindow(IntPtr h, bool alt);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out PT p);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(PT p);
  [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h, uint flags);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
  [StructLayout(LayoutKind.Sequential)] public struct PT { public int X; public int Y; }
  public const uint RIGHTDOWN = 0x0008, RIGHTUP = 0x0010, MOVE = 0x0001;
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
}
'@
Add-Type -TypeDefinition $src

$dir = 'D:\project\cpp\ltool\build\verify\menutest'
if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
Set-Content -LiteralPath "$dir\sample.txt" -Value 'menu probe' -Encoding ASCII

function Find-MyWindow {
  foreach ($w in $Auto::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, [System.Windows.Automation.Condition]::TrueCondition)) {
    if ($w.Current.ClassName -eq 'CabinetWClass' -and $w.Current.Name -like 'menutest*') { return $w }
  }
  return $null
}
$win = Find-MyWindow
if ($null -eq $win) {
  Start-Process explorer.exe -ArgumentList "`"$dir`"" | Out-Null
  Start-Sleep -Seconds 4
  $win = Find-MyWindow
}
if ($null -eq $win) { Write-Output 'NO-WINDOW'; exit 1 }
$hwnd = $win.Current.NativeWindowHandle
[W32]::SwitchToThisWindow($hwnd, $true)
Start-Sleep -Milliseconds 900
$null = [W32]::SetForegroundWindow($hwnd)
Start-Sleep -Milliseconds 400

$item = $null
foreach ($nm in @('sample.txt', 'sample')) {
  $itemCond = New-Object System.Windows.Automation.PropertyCondition($Auto::NameProperty, $nm)
  $item = $win.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $itemCond)
  if ($item) { Write-Output ('用名字 [' + $nm + '] 找到了行'); break }
}
$itemCond = $null
if ($null -eq $item) {
  Write-Output '窗口里能看到的行：'
  foreach ($ct in @([System.Windows.Automation.ControlType]::ListItem, [System.Windows.Automation.ControlType]::Custom,
                    [System.Windows.Automation.ControlType]::Text, [System.Windows.Automation.ControlType]::DataItem)) {
    $cc = New-Object System.Windows.Automation.PropertyCondition($Auto::ControlTypeProperty, $ct)
    foreach ($e in $win.FindAll([System.Windows.Automation.TreeScope]::Descendants, $cc)) {
      if ($e.Current.Name) { Write-Output ('   ' + $ct.ProgrammaticName + ' [' + $e.Current.Name + ']') }
    }
  }
  Write-Output 'NO-ITEM'; exit 1
}
$r = $item.Current.BoundingRectangle
$cx = [int]($r.X + $r.Width / 2); $cy = [int]($r.Y + $r.Height / 2)
$pt = New-Object W32+PT; $pt.X = $cx; $pt.Y = $cy
$root = [W32]::GetAncestor([W32]::WindowFromPoint($pt), 2)
Write-Output ('click-target ok=' + ($root -eq $hwnd) + ' at ' + $cx + ',' + $cy)
if ($root -ne $hwnd) { Write-Output 'GUARD-FAIL'; exit 1 }

$save = New-Object W32+PT
$null = [W32]::GetCursorPos([ref]$save)
$null = [W32]::SetCursorPos($cx, $cy)
Start-Sleep -Milliseconds 200
[W32]::mouse_event([W32]::RIGHTDOWN, 0, 0, 0, [UIntPtr]::Zero)
[W32]::mouse_event([W32]::RIGHTUP, 0, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 1500

$mc = New-Object System.Windows.Automation.PropertyCondition($Auto::ControlTypeProperty, [System.Windows.Automation.ControlType]::MenuItem)
$popup = $null
foreach ($w in $Auto::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, [System.Windows.Automation.Condition]::TrueCondition)) {
  $n = @($w.FindAll([System.Windows.Automation.TreeScope]::Children, $mc))
  if ($n.Count -ge 3 -and $w.Current.ProcessId -eq $win.Current.ProcessId) { $popup = $w; break }
}
if ($null -eq $popup) { Write-Output 'NO-POPUP'; $null = [W32]::SetCursorPos($save.X, $save.Y); exit 1 }
Write-Output ('popup class=' + $popup.Current.ClassName + ' 顶层项数=' + @($popup.FindAll([System.Windows.Automation.TreeScope]::Children, $mc)).Count)
$target = $null
foreach ($mi in $popup.FindAll([System.Windows.Automation.TreeScope]::Descendants, $mc)) {
  $nm = $mi.Current.Name
  Write-Output ('  项 [' + $nm + '] expandable=' + $mi.Current.IsOffscreen + ' rect=' + [math]::Round($mi.Current.BoundingRectangle.Width))
  if ($nm -match 'ltool') { $target = $mi }
}
if ($null -eq $target) { Write-Output 'NO-LTOOL-ITEM'; $null = [W32]::SetCursorPos($save.X, $save.Y); exit 1 }

$ec = $null
if ($target.TryGetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern, [ref]$ec)) {
  Write-Output 'ltool 有 ExpandCollapse，展开它'
  $ec.Expand()
  Start-Sleep -Milliseconds 1200
} else {
  Write-Output 'ltool 没有 ExpandCollapse 模式'
}
# 重新扫所有弹出窗口，找子菜单里的项
$kids = @()
foreach ($w in $Auto::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, [System.Windows.Automation.Condition]::TrueCondition)) {
  $n = @($w.FindAll([System.Windows.Automation.TreeScope]::Descendants, $mc))
  if ($n.Count -eq 0) { continue }
  foreach ($e in $n) {
    $t = $e.Current.Name
    if ($t -match '解压|压缩|粉碎|占用|ltool') { $kids += ($w.Current.ClassName + ' :: ' + $t) }
  }
}
Write-Output ('子菜单里匹配到的项数=' + $kids.Count)
$kids | ForEach-Object { Write-Output ('   ' + $_) }
$null = [W32]::SetCursorPos($save.X, $save.Y)
[W32]::keybd_event(0x1B,0,0,[UIntPtr]::Zero)   # ESC 关掉菜单
[W32]::keybd_event(0x1B,0,2,[UIntPtr]::Zero)
Write-Output 'PROBE-DONE'
