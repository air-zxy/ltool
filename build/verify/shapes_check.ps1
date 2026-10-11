$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public struct PT { public int x; public int y; }
public static class WC {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
  [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(PT p);
  [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h, uint flags);
}
"@
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$root = $AE::RootElement
$out = New-Object System.Collections.Generic.List[string]
function Say([string]$s) { $out.Add($s) }
function Save-Out { [System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\shapes.txt', $out, (New-Object System.Text.UTF8Encoding($true))) }
trap { Save-Out; break }
function Warp([int]$x,[int]$y) { [WC]::SetCursorPos($x,$y) | Out-Null; Start-Sleep -Milliseconds 120 }
function Down { [WC]::mouse_event(0x0002,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 60 }
function Up { [WC]::mouse_event(0x0004,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 250 }
function Key([byte]$vk) { [WC]::keybd_event($vk,0,0,[IntPtr]::Zero); [WC]::keybd_event($vk,0,2,[IntPtr]::Zero); Start-Sleep -Milliseconds 120 }
function Drag([int]$x1,[int]$y1,[int]$x2,[int]$y2) { Warp $x1 $y1; Down; Warp $x2 $y2; Up }
function Shot([int]$x,[int]$y,[int]$w,[int]$h,[string]$name) {
  $bmp = [System.Drawing.Bitmap]::new([Math]::Max(1,$w), [Math]::Max(1,$h))
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($x, $y, 0, 0, $bmp.Size)
  $g.Dispose(); $bmp.Save($name, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
  Say ('saved ' + (Split-Path $name -Leaf))
}
function Ctrl($el, $type) { return @($el.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $type))) | ForEach-Object { $_ }) }
function Wins { return @($root.FindAll($TC::Children, $cond) | ForEach-Object { $_ }) }
# 全屏浮层永远在最上面，坐标点上的根窗口就是它自己；仍然复查一次再点
function ClickPt([int]$x, [int]$y, [string]$what) {
  $pt = New-Object PT; $pt.x = $x; $pt.y = $y
  $at = [WC]::GetAncestor([WC]::WindowFromPoint($pt), 2)
  if ($at -ne $script:ovh) { Say ("中止：(" + $x + "," + $y + ") 上是 " + $at + " 不是浮层 " + $script:ovh + "（" + $what + "）"); throw 'not overlay' }
  Warp $x $y; Down; Up
  Say ("click " + $what + " @ " + $x + "," + $y)
}
function DragPt([int]$x1,[int]$y1,[int]$x2,[int]$y2,[string]$what) {
  $pt = New-Object PT; $pt.x = $x1; $pt.y = $y1
  $at = [WC]::GetAncestor([WC]::WindowFromPoint($pt), 2)
  if ($at -ne $script:ovh) { Say ("中止：" + $what + " 起点不在浮层上"); throw 'not overlay' }
  Warp $x1 $y1; Down; Warp $x2 $y2; Up
  Say ("drag " + $what + " " + $x1 + "," + $y1 + " -> " + $x2 + "," + $y2)
}

# ---- 1) 设置页开着时按 F1 也要能截图 ----
# --settings 只在冷启动生效，所以直接带参数起
$prev = Get-Process ltool -ErrorAction SilentlyContinue
if ($prev) { Stop-Process -Id $prev.Id -Force; Start-Sleep -Seconds 2 }
$proc = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -ArgumentList '--settings' -PassThru
Start-Sleep -Seconds 5
$cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)
Say ('pid = ' + $proc.Id)
$open = @(Wins)
Say ('设置页开着时的窗口数 = ' + $open.Count)
Key 0x70
Start-Sleep -Milliseconds 1500
$after = @(Wins)
Say ('按 F1 后的窗口数 = ' + $after.Count)
foreach ($w in $after) { Say ('   win ' + [int]$w.Current.BoundingRectangle.Width + 'x' + [int]$w.Current.BoundingRectangle.Y) }
$ov = @($after | Where-Object { $_.Current.BoundingRectangle.Width -ge 2500 })
if ($ov.Count -eq 0) { Say 'FAIL: 设置页开着时 F1 没弹出截图浮层'; Save-Out; exit 6 }
$script:ovh = [IntPtr]$ov[0].Current.NativeWindowHandle
Say ('浮层 hwnd = ' + $script:ovh)
# 浮层要能收到鼠标：拖一个选区出来，能进标注态就说明事件进来了
DragPt 800 460 1240 760 '框选'
Start-Sleep -Milliseconds 1200
$btns = @(Ctrl $ov[0] $CT::CheckBox) + @(Ctrl $ov[0] $CT::Button)
Say ('工具栏控件 = ' + $btns.Count)
if ($btns.Count -lt 6) { Say 'FAIL: 浮层收不到鼠标（没进标注态）'; Save-Out; exit 7 }

# ---- 2) 画一个矩形，然后点中它、拖手柄拉伸 ----
$sorted = @($btns | Sort-Object { $_.Current.BoundingRectangle.X })
$rectBtn = $sorted[1]     # 选区 / 矩形 / 圆 / 箭头 / 文字 / 马赛克
$rb = $rectBtn.Current.BoundingRectangle
ClickPt ([int]($rb.X + $rb.Width/2)) ([int]($rb.Y + $rb.Height/2)) '矩形工具'
DragPt 900 500 1100 650 '画矩形'
Start-Sleep -Milliseconds 400
Shot 860 460 400 340 'D:\project\cpp\ltool\build\verify\shapes_1_drawn.png'
$selBtn = $sorted[0]      # 回到"选区"（无工具）状态才能点选形状
$sb = $selBtn.Current.BoundingRectangle
ClickPt ([int]($sb.X + $sb.Width/2)) ([int]($sb.Y + $sb.Height/2)) '选区工具'
ClickPt 1000 575 '点矩形内部（选中）'
Start-Sleep -Milliseconds 400
Shot 860 460 400 340 'D:\project\cpp\ltool\build\verify\shapes_2_selected.png'
DragPt 1100 650 1210 740 '拖右下角手柄拉伸'
Start-Sleep -Milliseconds 400
Shot 860 460 400 340 'D:\project\cpp\ltool\build\verify\shapes_3_stretched.png'
DragPt 1000 575 1040 610 '拖矩形本体挪位置'
Start-Sleep -Milliseconds 400
Shot 860 460 400 340 'D:\project\cpp\ltool\build\verify\shapes_4_moved.png'
Key 0x1B
Save-Out
Write-Host 'done'
