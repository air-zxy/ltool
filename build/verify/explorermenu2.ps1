$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$Auto = [System.Windows.Automation.AutomationElement]
$src = @'
using System;
using System.Runtime.InteropServices;
public static class W32 {
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr parent, IntPtr after, string cls, string title);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint msg, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint msg, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern int GetMenuStringW(IntPtr hMenu, uint idItem, IntPtr buf, int max, uint flags);
  [DllImport("user32.dll")] public static extern int GetMenuItemCount(IntPtr hMenu);
  [DllImport("user32.dll")] public static extern IntPtr GetSubMenu(IntPtr hMenu, int pos);
  [DllImport("user32.dll")] public static extern uint GetMenuItemID(IntPtr hMenu, int pos);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc cb, IntPtr arg);
  public delegate bool EnumProc(IntPtr h, IntPtr arg);
  public const uint WM_CONTEXTMENU = 0x007B;
}
'@
Add-Type -TypeDefinition $src

$dir = 'D:\project\cpp\ltool\build\verify\expmenu'
# 用 -like 找，标题后缀（文件资源管理器）在不同系统版本上不一样
$win = $null
foreach ($w in $Auto::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, [System.Windows.Automation.Condition]::TrueCondition)) {
  if ($w.Current.ClassName -eq 'CabinetWClass' -and $w.Current.Name -like 'expmenu*') { $win = $w; break }
}
if ($null -eq $win) {
  Start-Process -FilePath explorer.exe -ArgumentList ('"' + $dir + '"') | Out-Null
  Start-Sleep -Seconds 4
  foreach ($w in $Auto::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, [System.Windows.Automation.Condition]::TrueCondition)) {
    if ($w.Current.ClassName -eq 'CabinetWClass' -and $w.Current.Name -like 'expmenu*') { $win = $w; break }
  }
  if ($null -eq $win) { Write-Output '开不出来资源管理器窗口，停手'; exit 1 }
}
Write-Output ('窗口 hwnd=' + $win.Current.NativeWindowHandle + ' pid=' + $win.Current.ProcessId)

$itemCond = New-Object System.Windows.Automation.PropertyCondition($Auto::NameProperty, 'afile.txt')
$item = $win.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $itemCond)
if ($null -eq $item) { Write-Output '没有 afile.txt 行'; exit 1 }
$r = $item.Current.BoundingRectangle
$x = [int]($r.X + $r.Width / 2); $y = [int]($r.Y + $r.Height / 2)
Write-Output ('行中心=' + $x + ',' + $y)

# 列表控件（DirectUIHWND）
$listHwnd = [IntPtr]::Zero
$child = [W32]::FindWindowExW($win.Current.NativeWindowHandle, [IntPtr]::Zero, 'DirectUIHWND', $null)
if ($child -eq [IntPtr]::Zero) { $child = [W32]::FindWindowExW($child, [IntPtr]::Zero, 'DirectUIHWND', $null) }
if ($child -eq [IntPtr]::Zero) {
  $sbgrip = [W32]::FindWindowExW($win.Current.NativeWindowHandle, [IntPtr]::Zero, 'SheetHostCtrl', $null)
  Write-Output ('DirectUIHWND 没找到，SheetHostCtrl=' + $sbgrip)
}
Write-Output ('DirectUIHWND=' + $child)
if ($child -eq [IntPtr]::Zero) { Write-Output '拿不到列表句柄，停手'; exit 1 }

$lParam = [IntPtr]((($y -shl 16) -bor ($x -band 0xFFFF)))
$null = [W32]::PostMessageW($child, [W32]::WM_CONTEXTMENU, $child, $lParam)
Start-Sleep -Milliseconds 1500

$found = 0
foreach ($w in $Auto::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, [System.Windows.Automation.Condition]::TrueCondition)) {
  $cn = $w.Current.ClassName
  if ($cn -notmatch 'Menu|Popup|Xaml|CoreWindow' -and $w.Current.ControlType.ProgrammaticName -notmatch 'Menu') { continue }
  $mc = New-Object System.Windows.Automation.PropertyCondition($Auto::ControlTypeProperty, [System.Windows.Automation.ControlType]::MenuItem)
  $items = @($w.FindAll([System.Windows.Automation.TreeScope]::Descendants, $mc))
  if ($items.Count -eq 0) { continue }
  $found++
  Write-Output ('--- 菜单窗口 class=' + $cn + ' pid=' + $w.Current.ProcessId + ' 项数=' + $items.Count)
  foreach ($e in $items) {
    $nm = $e.Current.Name
    if ($nm -match 'ltool' -or $nm -match '压缩' -or $nm -match '解压' -or $nm -match '粉碎' -or $nm -match '占用') {
      Write-Output ('    项 [' + $nm + ']')
    }
  }
}
if ($found -eq 0) { Write-Output '没抓到任何弹出菜单（PostMessage 这条路没生效）' }
