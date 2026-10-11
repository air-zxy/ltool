$ErrorActionPreference = 'Continue'
# 上一轮跑挂了会留下带 --pinshot 的测试实例（它们的窗口和 z 序会把这一轮的坐标全搞乱）
Get-CimInstance Win32_Process -Filter "Name='ltool.exe'" | Where-Object {
  $_.CommandLine -like '*--pinshot*' } | ForEach-Object {
  Write-Output ('先收掉上一轮的测试实例 PID=' + $_.ProcessId)
  Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
}
Start-Sleep -Milliseconds 800
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
$Auto = [System.Windows.Automation.AutomationElement]
$src = @'
using System;
using System.Runtime.InteropServices;
public static class M {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out PT p);
  [DllImport("user32.dll")] public static extern void SwitchToThisWindow(IntPtr h, bool alt);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint f, UIntPtr e);
  [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(PT p);
  [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h, uint f);
  public struct PT { public int X; public int Y; }
  public const uint LD = 0x0002, LU = 0x0004, RM = 0x0008, RU = 0x0010;
}
'@
Add-Type -TypeDefinition $src

$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
$dir = 'D:\project\cpp\ltool\build\verify\pintest'
$log = New-Object System.Collections.Generic.List[string]

if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $dir | Out-Null
# 中途炸了也要把已经测到的结论落盘，并且收掉自己起的进程（别留窗口干扰下一轮）
trap {
  $log.Add('中断：' + $_.Exception.Message)
  if ($script:p -and -not $script:p.HasExited) { Stop-Process -Id $script:p.Id -Force -ErrorAction SilentlyContinue }
  [System.IO.File]::WriteAllLines("$dir\_report.txt", $log, (New-Object System.Text.UTF8Encoding($true)))
  Write-Output ('中断：' + $_.Exception.Message)
  break
}
$imgPath = "$dir\base.png"
$bmp = New-Object System.Drawing.Bitmap 900, 500
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::FromArgb(232, 234, 240))
# 底图要有真实细节，否则"打码前后一模一样"根本验不出马赛克
for ($x = 0; $x -lt 900; $x += 12) {
  $g.FillRectangle([System.Drawing.Brushes]::Black, $x, 0, 6, 500)
}
for ($y = 40; $y -lt 500; $y += 60) {
  $g.FillRectangle([System.Drawing.Brushes]::Navy, 0, $y, 900, 4)
}
$g.Dispose()
$bmp.Save($imgPath, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()

function Guard($hwnd, $x, $y) {
  $p = New-Object M+PT; $p.X = $x; $p.Y = $y
  $found = [M]::WindowFromPoint($p)
  $root = [M]::GetAncestor($found, 2)
  if ($root -ne $hwnd) { $script:blockedBy = ('点了 ({0},{1}) 命中的是 hwnd={2}（root={3}），不是我={4}' -f $x, $y, $found, $root, $hwnd) }
  return $root -eq $hwnd
}
function Drag($hwnd, $x1, $y1, $x2, $y2) {
  # 置顶窗默认"失活就让位"，终端抢了前台它会沉下去：每次拖之前先把它抬起来
  [M]::SwitchToThisWindow($hwnd, $true)
  Start-Sleep -Milliseconds 400
  if (-not (Guard $hwnd $x1 $y1)) { throw ('守卫失败 ' + $script:blockedBy) }
  $null = [M]::SetCursorPos($x1, $y1); Start-Sleep -Milliseconds 120
  [M]::mouse_event([M]::LD, 0, 0, 0, [UIntPtr]::Zero)
  for ($i = 1; $i -le 10; $i++) {
    $null = [M]::SetCursorPos($x1 + [int](($x2 - $x1) * $i / 10), $y1 + [int](($y2 - $y1) * $i / 10))
    Start-Sleep -Milliseconds 25
  }
  [M]::mouse_event([M]::LU, 0, 0, 0, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 200
  # 按到空白处会变成拖窗口，后面所有坐标就全废了（还会点到别人桌面上去）：立刻停
  $r = $win.Current.BoundingRectangle
  if ([int]$r.X -ne $ox -or [int]$r.Y -ne $oy) {
    throw ("这一拖把窗口本身挪走了（{0},{1} → {2},{3}），说明按下去的位置没命中任何形状，停" -f $ox, $oy, [int]$r.X, [int]$r.Y)
  }
}
function ClickAt($hwnd, $x, $y) { Drag $hwnd $x $y ($x + 1) ($y + 1) }
function Key($vk) {
  [M]::keybd_event($vk, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60
  [M]::keybd_event($vk, 0, 2, [UIntPtr]::Zero); Start-Sleep -Milliseconds 200
}
function ClipPng($name) {
  $img = [System.Windows.Forms.Clipboard]::GetImage()
  if ($null -eq $img) { throw '剪贴板里没有图' }
  $p = Join-Path $dir $name
  $img.Save($p, [System.Drawing.Imaging.ImageFormat]::Png)
  $img.Dispose()
  return $p
}
# 数一下"描边红"像素落在某个矩形带里的数量
function StrokeIn($png, $x0, $y0, $x1, $y1) {
  $b = New-Object System.Drawing.Bitmap $png
  $n = 0
  for ($y = [Math]::Max(0, $y0); $y -lt [Math]::Min($b.Height, $y1); $y++) {
    for ($x = [Math]::Max(0, $x0); $x -lt [Math]::Min($b.Width, $x1); $x++) {
      $c = $b.GetPixel($x, $y)
      if ([Math]::Abs($c.R - 214) -lt 40 -and [Math]::Abs($c.G - 69) -lt 40 -and [Math]::Abs($c.B - 69) -lt 40) { $n++ }
    }
  }
  $b.Dispose()
  return $n
}
# 数一下"和底图不一样"的采样点（马赛克看不出新描边，只能这么验）
function DiffIn($png, $x0, $y0, $x1, $y1) {
  $b = New-Object System.Drawing.Bitmap $png
  $base = New-Object System.Drawing.Bitmap $imgPath
  $n = 0
  for ($y = [Math]::Max(0, $y0); $y -lt [Math]::Min($b.Height, $y1); $y += 2) {
    for ($x = [Math]::Max(0, $x0); $x -lt [Math]::Min($b.Width, $x1); $x += 2) {
      if ($b.GetPixel($x, $y).ToArgb() -ne $base.GetPixel($x, $y).ToArgb()) { $n++ }
    }
  }
  $b.Dispose(); $base.Dispose()
  return $n
}
function Press($el) {
  $ip = $null
  if ($el.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$ip)) { $ip.Invoke(); return 'invoke' }
  $tp = $null
  if ($el.TryGetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern, [ref]$tp)) { $tp.Toggle(); return 'toggle' }
  return '没有可点击的模式'
}
function FindButton($win, $helpPart) {
  # checkable 的 QPushButton 在 UIA 里是 CheckBox，不是 Button，两种都要认
  foreach ($ct in @([System.Windows.Automation.ControlType]::Button, [System.Windows.Automation.ControlType]::CheckBox)) {
    $bc = New-Object System.Windows.Automation.PropertyCondition($Auto::ControlTypeProperty, $ct)
    foreach ($b in $win.FindAll([System.Windows.Automation.TreeScope]::Descendants, $bc)) {
      if ($b.Current.Name -like ('*' + $helpPart + '*')) { return $b }
    }
  }
  return $null
}

$p = Start-Process -FilePath $exe -ArgumentList @('--pinshot', $imgPath) -PassThru
# 记下鼠标位置，跑完放回去（脚本要合成拖动，光标会动）
$saved = New-Object M+PT
$null = [M]::GetCursorPos([ref]$saved)
$sx = $saved.X; $sy = $saved.Y
Write-Output ("起始光标=$sx,$sy")
$log.Add("起始光标=$sx,$sy（结束会放回去）")
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
$log.Add("窗口 rect=$ox,$oy $($wr.Width)x$($wr.Height) hwnd=$hwnd")
# 图像左上角：左侧历史条 76px、底部工具栏 42px，图在剩下的区域里居中（不是贴左上！）
$areaW = [int]$wr.Width - 76; $areaH = [int]$wr.Height - 42
$imgOx = 76 + [int](($areaW - 900) / 2); $imgOy = [int](($areaH - 500) / 2)
$log.Add("图像原点（相对窗口）= $imgOx,$imgOy  区域=${areaW}x${areaH}")
function IP($ix, $iy) { $lx = $ox + $imgOx + $ix; $ly = $oy + $imgOy + $iy; return @($lx, $ly) }

$rectBtn = FindButton $win '矩形'
$selBtn = FindButton $win '拖动/选择'
$copyBtn = FindButton $win '复制'
$log.Add("找到按钮: 矩形=$($null -ne $rectBtn) 拖动/选择=$($null -ne $selBtn) 复制=$($null -ne $copyBtn)")
if (-not $rectBtn -or -not $selBtn -or -not $copyBtn) {
  $bc = New-Object System.Windows.Automation.PropertyCondition($Auto::ControlTypeProperty, [System.Windows.Automation.ControlType]::Button)
  foreach ($b in $win.FindAll([System.Windows.Automation.TreeScope]::Descendants, $bc)) {
    $r = $b.Current.BoundingRectangle
    Write-Output ('   BTN name=[' + $b.Current.Name + '] help=[' + $b.Current.HelpText + '] rect=' + [int]$r.X + ',' + [int]$r.Y + ' ' + [int]$r.Width + 'x' + [int]$r.Height)
  }
  [System.IO.File]::WriteAllLines("$dir\_report.txt", $log, (New-Object System.Text.UTF8Encoding($true)))
  Stop-Process -Id $p.Id -Force
  exit 1
}

# 1) 画一个矩形 (100,100)-(300,220)
$a = IP 100 100; $b = IP 300 220
Press $rectBtn; Start-Sleep -Milliseconds 300
Drag $hwnd $a[0] $a[1] $b[0] $b[1]
Press $copyBtn; Start-Sleep -Milliseconds 400
$png1 = ClipPng 'after-rect.png'
$n1 = StrokeIn $png1 95 95 306 226
$log.Add("1) 画矩形后：描边像素=$n1（期望 >300）")

# 2) 切到「拖动/选择」（刚画的会自动保持选中），拖内部 = 整体移动到 (160,140)-(360,260)
Press $selBtn; Start-Sleep -Milliseconds 300
$from = IP 150 130        # 内部、且不在任何手柄上（手柄在角和边中点）
$to = IP 210 170
Drag $hwnd $from[0] $from[1] $to[0] $to[1]
Press $copyBtn; Start-Sleep -Milliseconds 400
$png2 = ClipPng 'after-move.png'
$newBand = StrokeIn $png2 155 135 366 266
$oldBand = StrokeIn $png2 95 95 306 226
$log.Add("2) 移动后：新位置带内像素=$newBand 旧位置带内像素=$oldBand（期望 新>300 旧>0 但明显少于新）")

# 3) 拉右下角手柄放大
$hr = IP 360 260
$dragFrom = IP 360 260
$dragTo = IP 430 330
Drag $hwnd $dragFrom[0] $dragFrom[1] $dragTo[0] $dragTo[1]
Press $copyBtn; Start-Sleep -Milliseconds 400
$png3 = ClipPng 'after-resize.png'
$corner = StrokeIn $png3 424 324 436 336
$log.Add("3) 拉手柄后：右下角 (430,330) 附近有描边像素=$corner（期望 >20）")

# 4) Delete 删掉
$mid = IP 295 200
ClickAt $hwnd $mid[0] $mid[1]
Key 0x2E
Press $copyBtn; Start-Sleep -Milliseconds 400
$png4 = ClipPng 'after-delete.png'
$all = StrokeIn $png4 0 0 900 500
$log.Add("4) Delete 后：整张图描边像素=$all（期望 0）")

# 5) 马赛克拖框
$mos = FindButton $win '马赛克'
Press $mos; Start-Sleep -Milliseconds 300
$m1 = IP 400 60; $m2 = IP 620 180
Drag $hwnd $m1[0] $m1[1] $m2[0] $m2[1]
Press $copyBtn; Start-Sleep -Milliseconds 400
$png5 = ClipPng 'after-mosaic.png'
$inOld = DiffIn $png5 400 60 620 180
$log.Add("5a) 马赛克框 (400,60)-(620,180) 内与底图不同的采样点=$inOld（期望 >2000）")

# 5b) 把马赛克框整体挪到 (460,120)-(680,240)：旧位置必须还原成底图，新位置才打码
Press $selBtn; Start-Sleep -Milliseconds 300
$mpress = IP 510 120
$mdrop = IP 570 180
Drag $hwnd $mpress[0] $mpress[1] $mdrop[0] $mdrop[1]
Press $copyBtn; Start-Sleep -Milliseconds 400
$png6 = ClipPng 'after-mosaic-move.png'
# 旧框里"只有旧框才有"的那块（左上 60x60），挪走之后应该和底图一模一样
$oldFree = DiffIn $png6 402 62 458 118
# 新框独有的那块（右下角），应该满屏都是打码
$newIn = DiffIn $png6 624 184 678 238
# 这块底图是 12px 一条的黑竖纹，每 2px 采样下打码后约 36% 的点会和原图不同（5a 实测 2397/6600），
# 所以阈值按这个比例给，别按"全部不同"写死
$log.Add("5b) 挪走后旧角不同点=$oldFree（期望 0） 新角不同点=$newIn（期望 >120）")

# 6) 圆：画出来 + 拉右下角手柄改大小
$cir = FindButton $win '圆'
Press $cir; Start-Sleep -Milliseconds 300
$e1 = IP 100 300; $e2 = IP 220 420
Drag $hwnd $e1[0] $e1[1] $e2[0] $e2[1]
Press $copyBtn; Start-Sleep -Milliseconds 400
$pu = ClipPng 'after-ellipse.png'
$nEl = StrokeIn $pu 95 295 226 426
Press $selBtn; Start-Sleep -Milliseconds 300
$hbr = IP 220 420; $hto = IP 300 470
Drag $hwnd $hbr[0] $hbr[1] $hto[0] $hto[1]
Press $copyBtn; Start-Sleep -Milliseconds 400
$pv = ClipPng 'after-ellipse-resize.png'
# 圆的右下角是包围盒角，弧不过那里；拉大手后最右切点在 (300,385)
$nBr = StrokeIn $pv 294 379 306 391
# 原来圆的最右切点在 (220,360)，拉大手后那里应该没有弧了（这一条才是"真的改了大小"）
$back = StrokeIn $pv 214 354 228 368
$log.Add("6) 画圆带内描边=$nEl（期望 >200）；拉下手后最右切点 (300,385) 描边=$nBr（期望 >20），原最右切点 (220,360) 描边=$back（期望 0）")

# 7) 箭头：画出来 + 拖"箭头那一端"的手柄（箭头只有两端手柄，不是包围盒）
$arr = FindButton $win '箭头'
Press $arr; Start-Sleep -Milliseconds 300
$a1 = IP 700 60; $a2 = IP 860 140
Drag $hwnd $a1[0] $a1[1] $a2[0] $a2[1]
Press $copyBtn; Start-Sleep -Milliseconds 400
$pa = ClipPng 'after-arrow.png'
$nAr = StrokeIn $pa 695 55 866 146
Press $selBtn; Start-Sleep -Milliseconds 300
$hb = IP 860 140; $ho = IP 860 300
Drag $hwnd $hb[0] $hb[1] $ho[0] $ho[1]
Press $copyBtn; Start-Sleep -Milliseconds 400
$pw = ClipPng 'after-arrow-move.png'
$nHead = StrokeIn $pw 846 286 866 310
$nOld = StrokeIn $pw 846 126 866 146
$log.Add("7) 画箭头带内描边=$nAr（期望 >150）；拖箭头端到手柄 (860,300) 后 新头描边=$nHead（期望 >20）旧头描边=$nOld（期望 0）")

# 收尾：关窗
$closeBtn = FindButton $win '关闭'
if ($closeBtn) {
  Press $closeBtn | Out-Null
}
Start-Sleep -Milliseconds 800
if (-not $p.WaitForExit(2000)) { Stop-Process -Id $p.Id -Force; $log.Add('没关掉，强杀了自己起的进程') }
$log.Add('进程退出=' + $p.HasExited)
$null = [M]::SetCursorPos($sx, $sy)
$log.Add("光标已放回 $sx,$sy")
[System.IO.File]::WriteAllLines("$dir\_report.txt", $log, (New-Object System.Text.UTF8Encoding($true)))
Write-Output 'done'
