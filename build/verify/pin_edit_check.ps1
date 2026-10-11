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
$root = $AE::RootElement
function Say([string]$s) { Write-Host ('| ' + $s) }
function Warp([int]$x, [int]$y) { [W32]::SetCursorPos($x, $y) | Out-Null; Start-Sleep -Milliseconds 120 }
function Down { [W32]::mouse_event(0x0002,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 60 }
function Up   { [W32]::mouse_event(0x0004,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 250 }
function Key([byte]$vk) { [W32]::keybd_event($vk,0,0,[IntPtr]::Zero); [W32]::keybd_event($vk,0,2,[IntPtr]::Zero); Start-Sleep -Milliseconds 120 }
function Drag([int]$x1,[int]$y1,[int]$x2,[int]$y2) { Warp $x1 $y1; Down; Warp $x2 $y2; Up }

# Refuse to click unless our own window is really on top, so synthetic mouse
# events can never land on whatever else the user happens to be using.
function AssertForeground([int]$hwnd, [string]$what) {
  $cur = [W32]::GetForegroundWindow()
  $want = [IntPtr]$hwnd
  if ($cur -ne $want) {
    [W32]::SetForegroundWindow($want) | Out-Null
    Start-Sleep -Milliseconds 300
    $cur = [W32]::GetForegroundWindow()
  }
  if ($cur -ne $want) { Say ("ABORT: " + $what + " is not foreground"); Key 0x1B; exit 9 }
}
function Shot([int]$x,[int]$y,[int]$w,[int]$h,[string]$name) {
  $bmp = [System.Drawing.Bitmap]::new([Math]::Max(1,$w), [Math]::Max(1,$h))
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($x, $y, 0, 0, $bmp.Size)
  $g.Dispose()
  $bmp.Save($name, [System.Drawing.Imaging.ImageFormat]::Png)
  $bmp.Dispose()
  Say ('saved ' + $name)
}

$old = Get-Process ltool -ErrorAction SilentlyContinue
if ($old) { Say ('stopping running ltool pid=' + ($old.Id -join ',')); Stop-Process -Id $old.Id -Force; Start-Sleep -Seconds 2 }
$proc = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -PassThru
Start-Sleep -Seconds 4
$cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)

function Buttons([int]$hwnd) {
  $w = $root.FindFirst($TC::Children, (New-Object System.Windows.Automation.PropertyCondition($AE::NativeWindowHandleProperty, $hwnd)))
  if (-not $w) { return @() }
  $f = $w.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $CT::Button)))
  return @($f | ForEach-Object { $_ })
}
function TopWindows { return @($root.FindAll($TC::Children, $cond) | ForEach-Object { $_ }) }

# ---- 1. capture a region and pin it ----
Key 0x70
Start-Sleep -Milliseconds 1500
$ov = TopWindows
if ($ov.Count -eq 0) { Say 'FAIL: capture overlay did not open'; exit 2 }
Drag 700 380 1150 700
Start-Sleep -Milliseconds 900
$ov = TopWindows
$capHwnd = [int]$ov[0].Current.NativeWindowHandle
AssertForeground $capHwnd 'capture overlay'
$capBtns = Buttons $capHwnd | Sort-Object { $_.Current.BoundingRectangle.X }
Say ('capture toolbar buttons = ' + $capBtns.Count)
# order: xuanqu, rect, circle, arrow, text, mosaic, PIN
$pinBtn = $capBtns[6]
$r = $pinBtn.Current.BoundingRectangle
Drag ([int]($r.X + $r.Width/2)) ([int]($r.Y + $r.Height/2)) ([int]($r.X + $r.Width/2)) ([int]($r.Y + $r.Height/2))
Start-Sleep -Milliseconds 900

$pins = TopWindows | Where-Object { [int]$_.Current.NativeWindowHandle -ne $capHwnd }
if ($pins.Count -eq 0) { Say 'FAIL: pin window did not open'; exit 3 }
$hwnd = [int]$pins[0].Current.NativeWindowHandle
$wr = $pins[0].Current.BoundingRectangle
Say ('pin window = ' + [int]$wr.X + ',' + [int]$wr.Y + ' ' + [int]$wr.Width + 'x' + [int]$wr.Height)
AssertForeground $hwnd 'pin window'
$tb = Buttons $hwnd | Sort-Object { $_.Current.BoundingRectangle.X }
Say ('pin toolbar buttons = ' + $tb.Count)
foreach ($b in $tb) { $bb = $b.Current.BoundingRectangle; Say ('  btn ' + $b.Current.Name + ' @ ' + [int]$bb.X + ',' + [int]$bb.Y) }
Shot ([int]$wr.X) ([int]$wr.Y) ([int]$wr.Width) ([int]$wr.Height) 'D:\project\cpp\ltool\build\verify\pin_0_open.png'

function ClickBtn($list, [int]$i) {
  $bb = $list[$i].Current.BoundingRectangle
  Drag ([int]($bb.X + $bb.Width/2)) ([int]($bb.Y + $bb.Height/2)) ([int]($bb.X + $bb.Width/2)) ([int]($bb.Y + $bb.Height/2))
}
# image area starts after the 76px thumb strip; pick points inside it
$ix = [int]$wr.X + 130
$iy = [int]$wr.Y + 40
AssertForeground $hwnd 'pin window'
# ---- 2. rectangle ----
ClickBtn $tb 1
Drag $ix $iy ($ix + 120) ($iy + 70)
Shot ([int]$wr.X) ([int]$wr.Y) ([int]$wr.Width) ([int]$wr.Height) 'D:\project\cpp\ltool\build\verify\pin_1_rect.png'
# ---- 3. arrow ----
AssertForeground $hwnd 'pin window'
ClickBtn $tb 3
Drag ($ix + 20) ($iy + 110) ($ix + 180) ($iy + 170)
Shot ([int]$wr.X) ([int]$wr.Y) ([int]$wr.Width) ([int]$wr.Height) 'D:\project\cpp\ltool\build\verify\pin_2_arrow.png'
# ---- 4. mosaic ----
AssertForeground $hwnd 'pin window'
ClickBtn $tb 4
Warp ($ix + 60) ($iy + 210); Down; Warp ($ix + 200) ($iy + 215); Warp ($ix + 260) ($iy + 220); Up
Shot ([int]$wr.X) ([int]$wr.Y) ([int]$wr.Width) ([int]$wr.Height) 'D:\project\cpp\ltool\build\verify\pin_3_mosaic.png'
# ---- 5. undo back over the strokes ----
AssertForeground $hwnd 'pin window'
ClickBtn $tb 6
ClickBtn $tb 6
Shot ([int]$wr.X) ([int]$wr.Y) ([int]$wr.Width) ([int]$wr.Height) 'D:\project\cpp\ltool\build\verify\pin_4_undo2.png'
# ---- 6. crop ----
AssertForeground $hwnd 'pin window'
ClickBtn $tb 5
Drag ($ix - 40) ($iy - 20) ($ix + 150) ($iy + 120)
Start-Sleep -Milliseconds 600
$pins2 = TopWindows | Where-Object { [int]$_.Current.NativeWindowHandle -eq $hwnd }
if ($pins2.Count) {
  $r2 = $pins2[0].Current.BoundingRectangle
  Say ('after crop window = ' + [int]$r2.Width + 'x' + [int]$r2.Height)
  Shot ([int]$r2.X) ([int]$r2.Y) ([int]$r2.Width) ([int]$r2.Height) 'D:\project\cpp\ltool\build\verify\pin_5_crop.png'
}
# ---- 7. second capture should show 2 thumbnails on the left ----
Key 0x1B
Start-Sleep -Milliseconds 400
$capCount = (Get-ChildItem "$env:APPDATA\ltool\captures\cap-*.png" -ErrorAction SilentlyContinue).Count
Say ('captures on disk = ' + $capCount)
Key 0x70
Start-Sleep -Milliseconds 1500
Drag 640 300 1000 520
Start-Sleep -Milliseconds 900
$ov = TopWindows
$capHwnd2 = [int]$ov[0].Current.NativeWindowHandle
AssertForeground $capHwnd2 'capture overlay 2'
$tb2 = Buttons $capHwnd2 | Sort-Object { $_.Current.BoundingRectangle.X }
ClickBtn $tb2 6
Start-Sleep -Milliseconds 900
$p = TopWindows | Where-Object { [int]$_.Current.NativeWindowHandle -ne $capHwnd2 }
if ($p.Count) {
  $pr = $p[0].Current.BoundingRectangle
  Say ('pin2 window = ' + [int]$pr.Width + 'x' + [int]$pr.Height)
  Shot ([int]$pr.X) ([int]$pr.Y) ([int]$pr.Width) ([int]$pr.Height) 'D:\project\cpp\ltool\build\verify\pin_6_history.png'
}
Key 0x1B
Start-Sleep -Milliseconds 400
Say ('undo files on disk = ' + (Get-ChildItem "$env:APPDATA\ltool\captures\cap-*.png").Count)
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; Start-Sleep -Seconds 2 }
Start-Process 'D:\software\ltool\ltool.exe' | Out-Null
Start-Sleep -Seconds 3
Say ('relaunched installed pid=' + ((Get-Process ltool -ErrorAction SilentlyContinue).Id -join ','))
