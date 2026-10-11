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
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
}
"@
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$root = $AE::RootElement
$script:pid2 = 0
function Say([string]$s) { Write-Host ('| ' + $s) }
function Warp([int]$x, [int]$y) { [W32]::SetCursorPos($x, $y) | Out-Null; Start-Sleep -Milliseconds 120 }
function Down { [W32]::mouse_event(0x0002,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 60 }
function Up   { [W32]::mouse_event(0x0004,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 250 }
function Key([byte]$vk) { [W32]::keybd_event($vk,0,0,[IntPtr]::Zero); [W32]::keybd_event($vk,0,2,[IntPtr]::Zero); Start-Sleep -Milliseconds 120 }
function Drag([int]$x1,[int]$y1,[int]$x2,[int]$y2) { Warp $x1 $y1; Down; Warp $x2 $y2; Up }

# Safety: never send a click unless the foreground window belongs to OUR process,
# so a stray synthetic event can't land on whatever else the user is doing.
function AssertForeground([IntPtr]$h, [string]$what) {
  $fp = [uint32]0
  [W32]::GetWindowThreadProcessId([W32]::GetForegroundWindow(), [ref]$fp) | Out-Null
  if ([int]$fp -ne $script:pid2) {
    [W32]::SetForegroundWindow($h) | Out-Null
    Start-Sleep -Milliseconds 350
    $fp = [uint32]0
    [W32]::GetWindowThreadProcessId([W32]::GetForegroundWindow(), [ref]$fp) | Out-Null
  }
  if ([int]$fp -ne $script:pid2) { Say ("ABORT: foreground is not ltool (" + $what + ")"); exit 9 }
}
function Shot([int]$x,[int]$y,[int]$w,[int]$h,[string]$name) {
  $bmp = [System.Drawing.Bitmap]::new([Math]::Max(1,$w), [Math]::Max(1,$h))
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($x, $y, 0, 0, $bmp.Size)
  $g.Dispose()
  $bmp.Save($name, [System.Drawing.Imaging.ImageFormat]::Png)
  $bmp.Dispose()
  Say ('saved ' + (Split-Path $name -Leaf))
}

$old = Get-Process ltool -ErrorAction SilentlyContinue
if ($old) { Say ('stopping running ltool pid=' + ($old.Id -join ',')); Stop-Process -Id $old.Id -Force; Start-Sleep -Seconds 2 }
$proc = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -PassThru
$script:pid2 = $proc.Id
Start-Sleep -Seconds 4
$cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)
function TopWindows { return @($root.FindAll($TC::Children, $cond) | ForEach-Object { $_ }) }
function Buttons($win) {
  # checkable QPushButtons show up as ControlType.CheckBox, plain ones as Button
  $all = $win.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::IsControlElementProperty, $true)))
  return @($all | Where-Object { $_.Current.ControlType -eq $CT::Button -or $_.Current.ControlType -eq $CT::CheckBox } |
           ForEach-Object { $_ } | Sort-Object { $_.Current.BoundingRectangle.X })
}
function ClickBtn($list, [int]$i) {
  $bb = $list[$i].Current.BoundingRectangle
  Drag ([int]($bb.X + $bb.Width/2)) ([int]($bb.Y + $bb.Height/2)) ([int]($bb.X + $bb.Width/2)) ([int]($bb.Y + $bb.Height/2))
}

# ---- 1. capture a region, then pin it ----
Key 0x70
Start-Sleep -Milliseconds 1500
$ov = TopWindows
if ($ov.Count -eq 0) { Say 'FAIL: capture overlay did not open'; exit 2 }
AssertForeground $ov[0].Current.NativeWindowHandle 'overlay'
Drag 700 380 1150 700
Start-Sleep -Milliseconds 900
$ov = TopWindows
AssertForeground $ov[0].Current.NativeWindowHandle 'overlay2'
$capBtns = Buttons $ov[0]
Say ('capture toolbar buttons = ' + $capBtns.Count)
foreach ($b in $capBtns) { Say ('  cap btn [' + $b.Current.Name + ']') }
# order: xuanqu rect circle arrow text mosaic PIN undo copy save cancel
ClickBtn $capBtns 6
Start-Sleep -Milliseconds 1000
$pins = TopWindows
if ($pins.Count -eq 0) { Say 'FAIL: pin window did not open'; exit 3 }
$pw = $pins[0]
$wr = $pw.Current.BoundingRectangle
Say ('pin window = ' + [int]$wr.X + ',' + [int]$wr.Y + ' ' + [int]$wr.Width + 'x' + [int]$wr.Height)
AssertForeground $pw.Current.NativeWindowHandle 'pin'
$tb = Buttons $pw
Say ('pin toolbar buttons = ' + $tb.Count)
foreach ($b in $tb) { $bb = $b.Current.BoundingRectangle; Say ('  pin btn [' + $b.Current.Name + '] @ ' + [int]$bb.X + ',' + [int]$bb.Y + ' ' + [int]$bb.Width + 'x' + [int]$bb.Height) }
Shot ([int]$wr.X) ([int]$wr.Y) ([int]$wr.Width) ([int]$wr.Height) 'D:\project\cpp\ltool\build\verify\pin_0_open.png'

# image area starts right of the 76px thumb strip
$ix = [int]$wr.X + 130
$iy = [int]$wr.Y + 40
AssertForeground $pw.Current.NativeWindowHandle 'pin-rect'
ClickBtn $tb 1
Drag $ix $iy ($ix + 120) ($iy + 70)
Shot ([int]$wr.X) ([int]$wr.Y) ([int]$wr.Width) ([int]$wr.Height) 'D:\project\cpp\ltool\build\verify\pin_1_rect.png'

AssertForeground $pw.Current.NativeWindowHandle 'pin-arrow'
ClickBtn $tb 3
Drag ($ix + 20) ($iy + 110) ($ix + 180) ($iy + 170)
Shot ([int]$wr.X) ([int]$wr.Y) ([int]$wr.Width) ([int]$wr.Height) 'D:\project\cpp\ltool\build\verify\pin_2_arrow.png'

AssertForeground $pw.Current.NativeWindowHandle 'pin-mosaic'
ClickBtn $tb 4
Warp ($ix + 60) ($iy + 210); Down; Warp ($ix + 160) ($iy + 215); Warp ($ix + 260) ($iy + 220); Up
Shot ([int]$wr.X) ([int]$wr.Y) ([int]$wr.Width) ([int]$wr.Height) 'D:\project\cpp\ltool\build\verify\pin_3_mosaic.png'

AssertForeground $pw.Current.NativeWindowHandle 'pin-undo'
ClickBtn $tb 6
ClickBtn $tb 6
Shot ([int]$wr.X) ([int]$wr.Y) ([int]$wr.Width) ([int]$wr.Height) 'D:\project\cpp\ltool\build\verify\pin_4_undo2.png'

AssertForeground $pw.Current.NativeWindowHandle 'pin-crop'
ClickBtn $tb 5
Drag ($ix - 40) ($iy - 20) ($ix + 150) ($iy + 120)
Start-Sleep -Milliseconds 700
$again = TopWindows | Where-Object { $_.Current.NativeWindowHandle -eq $pw.Current.NativeWindowHandle }
if ($again) {
  $r2 = $again[0].Current.BoundingRectangle
  Say ('after crop = ' + [int]$r2.Width + 'x' + [int]$r2.Height)
  Shot ([int]$r2.X) ([int]$r2.Y) ([int]$r2.Width) ([int]$r2.Height) 'D:\project\cpp\ltool\build\verify\pin_5_crop.png'
}

# ---- 2. a second capture must show 2 thumbnails in the strip ----
Key 0x1B
Start-Sleep -Milliseconds 500
Say ('captures on disk = ' + (Get-ChildItem "$env:APPDATA\ltool\captures\cap-*.png" -ErrorAction SilentlyContinue).Count)
Key 0x70
Start-Sleep -Milliseconds 1500
$ov = TopWindows
if ($ov.Count -eq 0) { Say 'FAIL: 2nd overlay did not open'; exit 4 }
AssertForeground $ov[0].Current.NativeWindowHandle 'overlay-2'
Drag 640 300 1000 520
Start-Sleep -Milliseconds 900
$ov = TopWindows
AssertForeground $ov[0].Current.NativeWindowHandle 'overlay-2b'
ClickBtn (Buttons $ov[0]) 6
Start-Sleep -Milliseconds 1000
$p2 = TopWindows
if ($p2.Count) {
  $pr = $p2[0].Current.BoundingRectangle
  Say ('pin2 = ' + [int]$pr.Width + 'x' + [int]$pr.Height)
  Shot ([int]$pr.X) ([int]$pr.Y) ([int]$pr.Width) ([int]$pr.Height) 'D:\project\cpp\ltool\build\verify\pin_6_history.png'
  # click the 2nd thumbnail to prove the older capture is reachable
  $thumbX = [int]$pr.X + 38
  AssertForeground $p2[0].Current.NativeWindowHandle 'pin2-thumb-click'
  Drag $thumbX ([int]$pr.Y + 15) $thumbX ([int]$pr.Y + 15)
  Start-Sleep -Milliseconds 600
  $pr3 = (TopWindows | Where-Object { $_.Current.NativeWindowHandle -eq $p2[0].Current.NativeWindowHandle })[0].Current.BoundingRectangle
  Say ('after thumb click = ' + [int]$pr3.Width + 'x' + [int]$pr3.Height)
  Shot ([int]$pr3.X) ([int]$pr3.Y) ([int]$pr3.Width) ([int]$pr3.Height) 'D:\project\cpp\ltool\build\verify\pin_7_switched.png'
}
Key 0x1B
Start-Sleep -Milliseconds 400
Say ('captures on disk now = ' + (Get-ChildItem "$env:APPDATA\ltool\captures\cap-*.png").Count)
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; Start-Sleep -Seconds 2 }
Start-Process 'D:\project\cpp\ltool\build\Release\ltool.exe' | Out-Null
Start-Sleep -Seconds 2
Say ('relaunched pid=' + ((Get-Process ltool -ErrorAction SilentlyContinue).Id -join ','))
