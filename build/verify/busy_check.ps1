$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public struct PT { public int x; public int y; }
public static class WS {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(PT p);
  [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h, uint flags);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
}
"@
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$root = $AE::RootElement
$out = New-Object System.Collections.Generic.List[string]
function Say([string]$s) { $out.Add($s) }
function Save-Out { [System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\busy.txt', $out, (New-Object System.Text.UTF8Encoding($true))) }
trap { Save-Out; break }
function Ctrl($el, $type) { return @($el.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $type))) | ForEach-Object { $_ }) }
function Find($list, [string]$re) { return @($list | Where-Object { $_.Current.Name -match $re }) }
function Shot([int]$x,[int]$y,[int]$w,[int]$h,[string]$name) {
  $bmp = [System.Drawing.Bitmap]::new([Math]::Max(1,$w), [Math]::Max(1,$h))
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($x, $y, 0, 0, $bmp.Size)
  $g.Dispose(); $bmp.Save($name, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
}
function Inv($e, [string]$what) {
  $p = $e.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern); $p.Invoke(); Say ('invoke ' + $what)
}
function ClickIn($e, [string]$what, [IntPtr]$owner) {
  $r = $e.Current.BoundingRectangle
  $x = [int]($r.X + $r.Width/2); $y = [int]($r.Y + $r.Height/2)
  $pt = New-Object PT; $pt.x = $x; $pt.y = $y
  $at = [WS]::GetAncestor([WS]::WindowFromPoint($pt), 2)
  if ($at -ne $owner) {
    [WS]::keybd_event(0x12,0,0,[IntPtr]::Zero); [WS]::keybd_event(0x12,0,2,[IntPtr]::Zero)
    [WS]::SetForegroundWindow($owner) | Out-Null; Start-Sleep -Milliseconds 400
    $at = [WS]::GetAncestor([WS]::WindowFromPoint($pt), 2)
  }
  if ($at -ne $owner) { Say ("中止：" + $what + " 被挡住"); throw 'occluded' }
  [WS]::SetCursorPos($x, $y) | Out-Null; Start-Sleep -Milliseconds 100
  [WS]::mouse_event(0x0002,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 60
  [WS]::mouse_event(0x0004,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 200
  Say ("click " + $what + " @ " + $x + "," + $y)
}
function Settings($procId) { return @(Ctrl $root $CT::Window | Where-Object { $_.Current.ProcessId -eq $procId -and $_.Current.Name -match '设置' }) }

$prev = Get-Process ltool -ErrorAction SilentlyContinue
if ($prev) { Stop-Process -Id $prev.Id -Force; Start-Sleep -Seconds 2 }
$proc = Start-Process -FilePath 'D:\software\ltool\ltool.exe' -ArgumentList '--settings' -PassThru
Start-Sleep -Seconds 6
Say ('pid = ' + $proc.Id)
$sw = Settings $proc.Id
if ($sw.Count -eq 0) { Say 'FAIL: 设置窗口没开'; Save-Out; exit 5 }
$swH = [IntPtr]$sw[0].Current.NativeWindowHandle
ClickIn (Find (Ctrl $sw[0] $CT::ListItem) '^软件$')[0] '软件导航' $swH
Start-Sleep -Milliseconds 1500
$sw = Settings $proc.Id
$row = Find (Ctrl $sw[0] $CT::Text) '^7-Zip'
$rr = $row[0].Current.BoundingRectangle
$dl = @(Ctrl $sw[0] $CT::Button | Where-Object { $_.Current.Name -eq '下载' -and [Math]::Abs($_.Current.BoundingRectangle.Y - $rr.Y) -lt 40 })
Say ('7-Zip 下载按钮 = ' + $dl.Count)

# 点火，然后高频轮询设置窗口里那个"正在安装 N 项…"的加载标志
Inv $dl[0] '下载(7-Zip)'
$hit = $null
for ($k = 0; $k -lt 150; $k++) {
  # 只在设置窗口自己的树里找，别的全局搜索会撞到用户窗口里同样含"正在安装"的文字
  $cur = Settings $proc.Id
  if ($cur.Count) {
    $b = @(Find (Ctrl $cur[0] $CT::Button) '^正在安装')
    if ($b.Count -gt 0) {
      $hit = $b[0]
      $sw = $cur
      $swH = [IntPtr]$cur[0].Current.NativeWindowHandle
      Say ('抓到加载标志（第 ' + $k + ' 次轮询）: [' + $hit.Current.Name + ']')
      $r = $hit.Current.BoundingRectangle
      $swr = $cur[0].Current.BoundingRectangle
      Shot ([int]$swr.X) ([int]$r.Y - 12) 320 52 'D:\project\cpp\ltool\build\verify\busy_indicator.png'
      break
    }
  }
  Start-Sleep -Milliseconds 40
}
if (-not $hit) { Say 'FAIL: 没看到加载标志'; Save-Out; exit 6 }
ClickIn $hit '加载标志' $swH
Start-Sleep -Milliseconds 1500
$logw = @(Ctrl $root $CT::Window | Where-Object { $_.Current.ProcessId -eq $proc.Id -and $_.Current.Name -match '安装日志' })
Say ('安装日志窗口 = ' + $logw.Count)
if ($logw.Count -eq 0) { Say 'FAIL: 点加载标志没打开看台'; Save-Out; exit 7 }
$r = $logw[0].Current.BoundingRectangle
Shot ([int]$r.X) ([int]$r.Y) ([int]$r.Width) ([int]$r.Height) 'D:\project\cpp\ltool\build\verify\busy_logwin.png'
$cancel = Find (Ctrl $logw[0] $CT::Button) '取消安装'
Say ('取消安装按钮 = ' + $cancel.Count + ' 可用=' + $cancel[0].Current.IsEnabled)
Get-Content "$env:APPDATA\ltool\log.txt" -Tail 6 | ForEach-Object { Say ('  log: ' + $_) }
Save-Out
Write-Host 'done'
