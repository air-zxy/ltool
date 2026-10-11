$ErrorActionPreference = 'Continue'
# 置顶截图窗的"图上划词"：合成拖动只打自己起的窗口，每一步都有守卫
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
$Auto = [System.Windows.Automation.AutomationElement]
$src = @'
using System;
using System.Runtime.InteropServices;
public static class K {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out PT p);
  [DllImport("user32.dll")] public static extern void SwitchToThisWindow(IntPtr h, bool alt);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint f, UIntPtr e);
  [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(PT p);
  [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h, uint f);
  public struct PT { public int X; public int Y; }
  public const uint LD = 0x0002, LU = 0x0004;
}
'@
Add-Type -TypeDefinition $src

$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
$dir = 'D:\project\cpp\ltool\build\verify\picktest'
$log = New-Object System.Collections.Generic.List[string]
$imgPath = Join-Path $dir 'text.png'
$script:failed = 0

function Check($name, $got, $want) {
  if ($got -eq $want) { $script:log.Add("PASS $name = [$got]") }
  else { $script:log.Add("FAIL $name 期望 [$want] 实得 [$got]"); $script:failed++ }
}

if (-not (Test-Path -LiteralPath $imgPath)) { throw "先跑 maketext.ps1 生成 $imgPath" }

# 上一轮的残留窗口会抢坐标
Get-CimInstance Win32_Process -Filter "Name='ltool.exe'" | Where-Object {
  $_.CommandLine -like '*--pinshot*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
Start-Sleep -Milliseconds 600
# 剪贴板先塞一个哨兵值，好区分"真复制到了"和"根本没复制"
Set-Clipboard -Value 'SENTINEL'

$p = Start-Process -FilePath $exe -ArgumentList @('--pinshot', $imgPath) -PassThru
$saved = New-Object K+PT
$null = [K]::GetCursorPos([ref]$saved)
Start-Sleep -Seconds 3
$cond = New-Object System.Windows.Automation.PropertyCondition($Auto::ProcessIdProperty, $p.Id)
$win = $null
foreach ($w in $Auto::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $cond)) {
  if ($w.Current.ClassName -eq 'PinWindow') { $win = $w; break }
}
if ($null -eq $win) { Write-Output 'NO-WINDOW'; exit 1 }
$wr = $win.Current.BoundingRectangle
$hwnd = $win.Current.NativeWindowHandle
$ox = [int]$wr.X; $oy = [int]$wr.Y
# 图在 76px 历史条右边的区域里居中，比例 = min(区域/图)
$imgW = 640; $imgH = 180
$areaW = [int]$wr.Width - 76; $areaH = [int]$wr.Height - 42
$sc = [Math]::Min($areaW / $imgW, $areaH / $imgH)
$vw = [int]($imgW * $sc); $vh = [int]($imgH * $sc)
$ix = 76 + [int](($areaW - $vw) / 2); $iy = [int](($areaH - $vh) / 2)
$log.Add("窗口=$ox,$oy $($wr.Width)x$($wr.Height) 图像原点=+$ix,+$iy 比例=$sc")
function IP($x, $y) { return @($ox + $ix + [int]($x * $sc), $oy + $iy + [int]($y * $sc)) }

function Guard($x, $y) {
  $q = New-Object K+PT; $q.X = $x; $q.Y = $y
  return [K]::GetAncestor([K]::WindowFromPoint($q), 2) -eq $hwnd
}
function DragImg($x1, $y1, $x2, $y2) {
  $a = IP $x1 $y1; $b = IP $x2 $y2
  [K]::SwitchToThisWindow($hwnd, $true); Start-Sleep -Milliseconds 400
  if (-not (Guard $a[0] $a[1])) { throw "守卫失败：($x1,$y1)→($($a[0]),$($a[1])) 不是我的窗口" }
  $null = [K]::SetCursorPos($a[0], $a[1]); Start-Sleep -Milliseconds 120
  [K]::mouse_event([K]::LD, 0, 0, 0, [UIntPtr]::Zero)
  for ($i = 1; $i -le 8; $i++) {
    $null = [K]::SetCursorPos($a[0] + [int](($b[0] - $a[0]) * $i / 8), $a[1] + [int](($b[1] - $a[1]) * $i / 8))
    Start-Sleep -Milliseconds 25
  }
  [K]::mouse_event([K]::LU, 0, 0, 0, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 250
}
function ClickImg($x, $y) { DragImg $x $y ($x + 1) ($y + 1) }
function FindBtn($part) {
  foreach ($ct in @([System.Windows.Automation.ControlType]::Button, [System.Windows.Automation.ControlType]::CheckBox)) {
    $bc = New-Object System.Windows.Automation.PropertyCondition($Auto::ControlTypeProperty, $ct)
    foreach ($b in $win.FindAll([System.Windows.Automation.TreeScope]::Descendants, $bc)) {
      if ($b.Current.Name -like ('*' + $part + '*')) { return $b }
    }
  }
  return $null
}
function Key($vk) {
  [K]::keybd_event($vk, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60
  [K]::keybd_event($vk, 0, 2, [UIntPtr]::Zero); Start-Sleep -Milliseconds 200
}
function Press($el) {
  # checkable 的按钮在 UIA 里可能只给 Toggle 不给 Invoke，两种都试
  $ip = $null
  if ($el.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$ip)) { $ip.Invoke(); return }
  $tp = $null
  if ($el.TryGetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern, [ref]$tp)) { $tp.Toggle(); return }
  throw '按钮上没有可点击的模式'
}

$pick = FindBtn '划词'
if ($null -eq $pick) {
  $bc = New-Object System.Windows.Automation.PropertyCondition($Auto::ControlTypeProperty, [System.Windows.Automation.ControlType]::Button)
  foreach ($b in $win.FindAll([System.Windows.Automation.TreeScope]::Descendants, $bc)) {
    $log.Add('   BTN [' + $b.Current.Name + ']')
  }
  $log.Add('FAIL 找不到「划词」按钮'); $script:failed++
} else {
  # 进划词模式：第一次要等后台识别（实测这张 180px 高的图 ~100ms，放大 4 倍后也在半秒内）
  Press $pick
  Start-Sleep -Milliseconds 1800

  # 1) 横着拖过第一行的中间几个字：按字中心挑，不该越界
  Set-Clipboard -Value 'SENTINEL'
  DragImg 60 35 140 35
  Check '拖半行只选中带内的字' (Get-Clipboard) '件安装'

  # 2) 单击一个汉字 = 整词（引擎给中文一个汉字一个词）
  Set-Clipboard -Value 'SENTINEL'
  ClickImg 143 35
  Check '单击选整词' (Get-Clipboard) '路'

  # 3) 一个大框罩住前两行：整行都该要，跨行要换行
  Set-Clipboard -Value 'SENTINEL'
  DragImg 0 20 200 100
  Check '罩住两行=两行全文' (Get-Clipboard) "软件安装路径`n默认搜索引擎"

  # 4) 英文词：一个词是一格，点一下选整个词，词之间补空格
  Set-Clipboard -Value 'SENTINEL'
  ClickImg 60 135
  Check '单击选英文整词' (Get-Clipboard) 'Search'

  # 5) Enter = 复制整张图
  Set-Clipboard -Value 'SENTINEL'
  [K]::SwitchToThisWindow($hwnd, $true); Start-Sleep -Milliseconds 300
  Key 0x0D
  Check 'Enter 复制全部' (Get-Clipboard) "软件安装路径`n默认搜索引擎`nSearch Engine 123"

  # 6) 窗还开着，图也还在（就地复制不该关窗）
  $still = $Auto::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Children, $cond)
  if ($null -ne $still) { $log.Add('PASS 复制完窗口还在') } else { $log.Add('FAIL 复制完窗口没了'); $script:failed++ }

  # 7) Esc 退出划词，再 Esc 关窗
  Key 0x1B
  Start-Sleep -Milliseconds 300
  $log.Add('按了一次 Esc（应退出划词、不该关窗）')
  $still2 = $Auto::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Children, $cond)
  if ($null -ne $still2) { $log.Add('PASS Esc 只退出划词') } else { $log.Add('FAIL Esc 把窗关了'); $script:failed++ }
}

$close = FindBtn '关闭'
if ($close) { Press $close }
Start-Sleep -Milliseconds 600
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
$null = [K]::SetCursorPos($saved.X, $saved.Y)
$log.Add("结论：失败 $script:failed 项")
[System.IO.File]::WriteAllLines((Join-Path $dir '_report.txt'), $log, (New-Object System.Text.UTF8Encoding($true)))
$log | ForEach-Object { Write-Output $_ }
exit $script:failed
