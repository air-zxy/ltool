$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$Auto = [System.Windows.Automation.AutomationElement]
$src = @'
using System;
using System.Runtime.InteropServices;
public static class U32 {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out PPOINT p);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(PPOINT p);
  [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h, uint flags);
  [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
  [StructLayout(LayoutKind.Sequential)] public struct PPOINT { public int X; public int Y; }
  public const uint RIGHTDOWN = 0x0008, RIGHTUP = 0x0010;
}
'@
Add-Type -TypeDefinition $src

$dir = 'D:\project\cpp\ltool\build\verify\expmenu'
if (-not (Test-Path -LiteralPath "$dir\afolder")) { New-Item -ItemType Directory -Force -Path "$dir\afolder" | Out-Null }
Set-Content -LiteralPath "$dir\afile.txt" -Value 'menu test' -Encoding ASCII

$prev = Start-Process -FilePath 'explorer.exe' -ArgumentList "`"$dir`"" -PassThru
Start-Sleep -Seconds 3
$win = $null
foreach ($w in $Auto::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, [System.Windows.Automation.Condition]::TrueCondition)) {
  if ($w.Current.ClassName -eq 'CabinetWClass' -and $w.Current.Name -like 'expmenu*') { $win = $w; break }
}
if ($null -eq $win) { Write-Output '没找到我开的资源管理器窗口'; exit 1 }
Write-Output ('窗口=' + $win.Current.Name + ' pid=' + $win.Current.ProcessId)
$null = [U32]::SetForegroundWindow($win.Current.NativeWindowHandle)
Start-Sleep -Milliseconds 500

$cond = New-Object System.Windows.Automation.PropertyCondition($Auto::NameProperty, 'afile.txt')
$item = $win.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
if ($null -eq $item) { Write-Output '没找到 afile.txt 这一行'; exit 1 }
$r = $item.Current.BoundingRectangle
$cx = [int]($r.X + $r.Width / 2); $cy = [int]($r.Y + $r.Height / 2)
Write-Output ('行矩形=' + $r + ' 目标点=' + $cx + ',' + $cy)

# 点击守卫：那个点必须真的属于我开的这个窗口
$pt = New-Object U32+PPOINT; $pt.X = $cx; $pt.Y = $cy
$hUnder = [U32]::WindowFromPoint($pt)
$rootUnder = [U32]::GetAncestor($hUnder, 2)   # GA_ROOT
Write-Output ('点上方的根窗口=' + $rootUnder + ' 我的窗口=' + $win.Current.NativeWindowHandle +
               ' 匹配=' + ($rootUnder -eq $win.Current.NativeWindowHandle))
if ($rootUnder -ne $win.Current.NativeWindowHandle) {
  Add-Type -AssemblyName Microsoft.VisualBasic
  try { $null = [Microsoft.VisualBasic.Interaction]::AppActivate('expmenu') } catch { Write-Output ('AppActivate 失败: ' + $_.Exception.Message) }
  Start-Sleep -Milliseconds 900
  $hUnder = [U32]::WindowFromPoint($pt); $rootUnder = [U32]::GetAncestor($hUnder, 2)
  $we = $Auto::RootElement.FromHandle($hUnder)
  $re = $Auto::RootElement.FromHandle($rootUnder)
  Write-Output ('再试一次: 匹配=' + ($rootUnder -eq $win.Current.NativeWindowHandle) + ' 该点上方的窗口 class=' + $we.Current.ClassName + ' name=[' + $we.Current.Name + '] 根=[' + $re.Current.Name + ']')
  if ($rootUnder -ne $win.Current.NativeWindowHandle) { Write-Output '守卫仍不通过，停手（不动鼠标）'; exit 1 }
}

$save = New-Object U32+PPOINT
$null = [U32]::GetCursorPos([ref]$save)
[U32]::mouse_event([U32]::RIGHTDOWN, 0, 0, 0, [UIntPtr]::Zero)
[U32]::mouse_event([U32]::RIGHTUP, 0, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 1200

# 找弹出来的菜单
$menus = @()
foreach ($w in $Auto::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, [System.Windows.Automation.Condition]::TrueCondition)) {
  if ($w.Current.ControlType.ProgrammaticName -match 'Menu' -or $w.Current.ClassName -match 'Menu|Popup|Xaml') {
    $menus += $w
  }
}
Write-Output ('顶层菜单窗口数=' + $menus.Count)
foreach ($m in $menus) {
  Write-Output ('--- 窗口 class=' + $m.Current.ClassName + ' name=[' + $m.Current.Name + '] pid=' + $m.Current.ProcessId)
  $mc = New-Object System.Windows.Automation.PropertyCondition($Auto::ControlTypeProperty, [System.Windows.Automation.ControlType]::MenuItem)
  foreach ($e in $m.FindAll([System.Windows.Automation.TreeScope]::Descendants, $mc)) {
    Write-Output ('    菜单项 [' + $e.Current.Name + ']')
  }
}
$null = [U32]::SetCursorPos($save.X, $save.Y)
