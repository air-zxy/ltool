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
}
"@
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$IP = [System.Windows.Automation.InvokePattern]
$root = $AE::RootElement
$script:pid2 = 0
$out = New-Object System.Collections.Generic.List[string]
function Say([string]$s) { $out.Add($s) }
function Shot([int]$x,[int]$y,[int]$w,[int]$h,[string]$name) {
  $bmp = [System.Drawing.Bitmap]::new([Math]::Max(1,$w), [Math]::Max(1,$h))
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($x, $y, 0, 0, $bmp.Size)
  $g.Dispose(); $bmp.Save($name, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
  Say ('saved ' + (Split-Path $name -Leaf))
}
function Wins { return @($root.FindAll($TC::Children, $cond) | ForEach-Object { $_ }) }
function Ctrl($el, $type) { return @($el.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $type))) | ForEach-Object { $_ }) }
function Find($list, [string]$re) { return @($list | Where-Object { $_.Current.Name -match $re }) }
function Save-Out { [System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\tray_check.txt', $out, (New-Object System.Text.UTF8Encoding($true))) }

# UIA patterns = the control's own action: no cursor, no z-order, so a synthetic
# click can never land on whatever else the user happens to have open.
# (PS 5.1's AutomationElement has no IsPatternSupported, so probe by asking.)
function Press($e, [string]$what) {
  if ($null -eq $e) { Say ("FAIL: " + $what + " 不存在"); throw 'missing element' }
  $isItem = ($e.Current.ControlType -eq $CT::ListItem)
  $order = if ($isItem) { @('sel','inv') } else { @('inv','sel') }
  foreach ($k in $order) {
    try {
      if ($k -eq 'sel') {
        $s = $e.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern)
        if ($s) { $s.Select(); Say ("select " + $what); return }
      } else {
        $p = $e.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
        if ($p) { $p.Invoke(); Say ("invoke " + $what); return }
      }
    } catch { }
  }
  Say ("skip: " + $what + " 既不支持 Invoke 也不支持 SelectionItem")
  throw 'no pattern'
}

$proc = Get-Process ltool -ErrorAction Stop
$script:pid2 = $proc.Id
$cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)
Say ('pid = ' + $proc.Id)

$sw = Find (Wins) '设置'
if ($sw.Count -eq 0) { Say 'FAIL: 设置窗口没开'; Save-Out; exit 5 }
$script:hwnd = $sw[0].Current.NativeWindowHandle
Say ('设置窗口: ' + $sw[0].Current.Name)

# ---- 软件页：点一个"已经装好"的包的下载，winget 只会回"已是最新"，不改系统 ----
Press (Find (Ctrl $sw[0] $CT::ListItem) '^软件$')[0] '软件 tab'
Start-Sleep -Milliseconds 1800
$rows = Ctrl $sw[0] $CT::ListItem
Say ('页面条目 = ' + $rows.Count)
foreach ($r in $rows) { if ($r.Current.Name.Length -gt 0) { Say ('  row [' + $r.Current.Name.Substring(0,[Math]::Min(64,$r.Current.Name.Length)) + ']') } }
$row = Find $rows '7-Zip'
if ($row.Count -eq 0) { Say 'FAIL: 找不到 7-Zip 行'; Save-Out; exit 6 }
$rr = $row[0].Current.BoundingRectangle
$btns = Ctrl $sw[0] $CT::Button
$same = @($btns | Where-Object { $b = $_.Current.BoundingRectangle; $b.Y -ge ($rr.Y - 8) -and $b.Y -lt ($rr.Y + $rr.Height + 8) -and $b.X -gt ($rr.X + 200) })
Say ('7-Zip 行内按钮 = ' + $same.Count + ' 行 @ ' + [int]$rr.X + ',' + [int]$rr.Y)
if ($same.Count -eq 0) { Save-Out; exit 7 }
foreach ($b in $same) { Say ('  btn [' + $b.Current.Name + ']') }
Press $same[0] '下载按钮'
Start-Sleep -Milliseconds 2500
foreach ($t in (Find (Ctrl $sw[0] $CT::Text) '安装')) { Say ('  状态: ' + $t.Current.Name) }

# ---- 托盘：展开隐藏图标面板，看图标在不在转 ----
$overflow = @(Find (Ctrl $root $CT::Button) '隐藏的图标')
if ($overflow.Count -eq 0) { Say 'FAIL: 找不到托盘溢出按钮'; Save-Out; exit 8 }
Press $overflow[0] '托盘溢出'
Start-Sleep -Milliseconds 1200
$lt = Find (Ctrl $root $CT::Button) 'ltool'
Say ('托盘里的 ltool 图标 = ' + $lt.Count)
if ($lt.Count -eq 0) {
  foreach ($b in (Ctrl $root $CT::Button)) { if ($b.Current.BoundingRectangle.Y -gt 1000) { Say ('  底部按钮 [' + $b.Current.Name + ']') } }
  Say 'FAIL: 溢出面板里找不到 ltool'; Save-Out; exit 9
}
$lr = $lt[0].Current.BoundingRectangle
Say ('ltool 托盘图标 @ ' + [int]$lr.X + ',' + [int]$lr.Y + ' name=' + $lt[0].Current.Name)
Shot ([int]$lr.X - 70) ([int]$lr.Y - 50) 240 130 'D:\project\cpp\ltool\build\verify\tray_busy.png'
Start-Sleep -Milliseconds 700
Shot ([int]$lr.X - 70) ([int]$lr.Y - 50) 240 130 'D:\project\cpp\ltool\build\verify\tray_busy2.png'
Press $lt[0] '托盘图标'
Start-Sleep -Milliseconds 2200
foreach ($w in (Wins)) { Say ('  窗口 "' + $w.Current.Name + '"') }
$logw = @((Wins) | Where-Object { $_.Current.Name -match '安装日志' })
Say ('安装日志窗口 = ' + $logw.Count)
if ($logw.Count -eq 0) { Say 'FAIL: 点托盘没有打开看台'; Save-Out; exit 10 }
$cancel = Find (Ctrl $logw[0] $CT::Button) '取消安装'
Say ('取消安装按钮 = ' + $cancel.Count)
if ($cancel.Count -eq 0) { Save-Out; exit 11 }
Say ('按钮可用 = ' + $cancel[0].Current.IsEnabled)
$lr2 = $logw[0].Current.BoundingRectangle
Shot ([int]$lr2.X) ([int]$lr2.Y) ([int]$lr2.Width) ([int]$lr2.Height) 'D:\project\cpp\ltool\build\verify\logwin_busy.png'
Press $cancel[0] '取消安装'
Start-Sleep -Milliseconds 4000
Shot ([int]$lr2.X) ([int]$lr2.Y) ([int]$lr2.Width) ([int]$lr2.Height) 'D:\project\cpp\ltool\build\verify\logwin_cancelled.png'
Get-Content "$env:APPDATA\ltool\log.txt" -Tail 10 | ForEach-Object { Say ('  log: ' + $_) }
Save-Out
Write-Host 'done'
