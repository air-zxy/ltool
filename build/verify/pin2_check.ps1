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
function AssertForeground([IntPtr]$h, [string]$what) {
  $fp = [uint32]0
  [W32]::GetWindowThreadProcessId([W32]::GetForegroundWindow(), [ref]$fp) | Out-Null
  if ([int]$fp -ne $script:pid2) {
    [W32]::SetForegroundWindow($h) | Out-Null; Start-Sleep -Milliseconds 350
    $fp = [uint32]0
    [W32]::GetWindowThreadProcessId([W32]::GetForegroundWindow(), [ref]$fp) | Out-Null
  }
  if ([int]$fp -ne $script:pid2) { Say ("ABORT: foreground is not ltool (" + $what + ")"); exit 9 }
}
function Shot([int]$x,[int]$y,[int]$w,[int]$h,[string]$name) {
  $bmp = [System.Drawing.Bitmap]::new([Math]::Max(1,$w), [Math]::Max(1,$h))
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($x, $y, 0, 0, $bmp.Size)
  $g.Dispose(); $bmp.Save($name, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
  Say ('saved ' + (Split-Path $name -Leaf))
}
function TopWindows { return @($root.FindAll($TC::Children, $cond) | ForEach-Object { $_ }) }
function Buttons($win) {
  $all = $win.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::IsControlElementProperty, $true)))
  return @($all | Where-Object { $_.Current.ControlType -eq $CT::Button -or $_.Current.ControlType -eq $CT::CheckBox } |
           ForEach-Object { $_ } | Sort-Object { $_.Current.BoundingRectangle.X })
}
function ClickBtn($list, [int]$i) {
  $bb = $list[$i].Current.BoundingRectangle
  Drag ([int]($bb.X + $bb.Width/2)) ([int]($bb.Y + $bb.Height/2)) ([int]($bb.X + $bb.Width/2)) ([int]($bb.Y + $bb.Height/2))
}
function PinOne([int]$sx,[int]$sy,[int]$ex,[int]$ey) {
  Key 0x70
  Start-Sleep -Milliseconds 1600
  $ov = TopWindows
  if ($ov.Count -eq 0) { Say 'FAIL: overlay missing'; exit 2 }
  AssertForeground $ov[0].Current.NativeWindowHandle 'overlay'
  Drag $sx $sy $ex $ey
  Start-Sleep -Milliseconds 1000
  $ov = TopWindows
  AssertForeground $ov[0].Current.NativeWindowHandle 'overlay-after-drag'
  ClickBtn (Buttons $ov[0]) 6        # 6th control on the capture bar = pin
  Start-Sleep -Milliseconds 1100
  $p = TopWindows
  if ($p.Count -eq 0) { Say 'FAIL: pin window missing'; exit 3 }
  return $p[0]
}

$old = Get-Process ltool -ErrorAction SilentlyContinue
if ($old) { Say ('stopping running ltool pid=' + ($old.Id -join ',')); Stop-Process -Id $old.Id -Force; Start-Sleep -Seconds 2 }
$proc = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -PassThru
$script:pid2 = $proc.Id
Start-Sleep -Seconds 4
$cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)

# capture #1 (will become the *older* entry after capture #2)
$a = PinOne 300 300 1000 780
$ra = $a.Current.BoundingRectangle
Say ('pin#1 = ' + [int]$ra.X + ',' + [int]$ra.Y + ' ' + [int]$ra.Width + 'x' + [int]$ra.Height)
Key 0x1B
Start-Sleep -Milliseconds 500

# capture #2, then mosaic a band across it and shoot that band 1:1
$b = PinOne 1200 200 2100 900
$rb = $b.Current.BoundingRectangle
Say ('pin#2 = ' + [int]$rb.X + ',' + [int]$rb.Y + ' ' + [int]$rb.Width + 'x' + [int]$rb.Height)
AssertForeground $b.Current.NativeWindowHandle 'pin2-tools'
$tb = Buttons $b
Say ('pin toolbar = ' + $tb.Count + ' buttons')
ClickBtn $tb 4          # mosaic
$by = [int]$rb.Y + 150
Warp ([int]$rb.X + 150) $by
Down
Warp ([int]$rb.X + 400) ($by + 6)
Warp ([int]$rb.X + 650) ($by + 10)
Warp ([int]$rb.X + 820) ($by + 12)
Up
Start-Sleep -Milliseconds 400
Shot ([int]$rb.X + 100) ($by - 40) 900 100 'D:\project\cpp\ltool\build\verify\p2_mosaic_band.png'
$undoTxt = $tb[6].Current.Name
Say ('undo button now reads: ' + $undoTxt)
ClickBtn $tb 6          # undo the mosaic
Start-Sleep -Milliseconds 400
Shot ([int]$rb.X + 100) ($by - 40) 900 100 'D:\project\cpp\ltool\build\verify\p2_after_undo.png'

# history strip: click the SECOND thumbnail (older capture) and check the main image changed
$tb2 = Buttons (TopWindows | Where-Object { $_.Current.NativeWindowHandle -eq $b.Current.NativeWindowHandle })[0]
AssertForeground $b.Current.NativeWindowHandle 'pin2-thumb'
$th1 = [int]$rb.Y + 4 + 58 + 20         # centre of the 2nd thumb row
Drag ([int]$rb.X + 38) $th1 ([int]$rb.X + 38) $th1
Start-Sleep -Milliseconds 700
$cur = TopWindows | Where-Object { $_.Current.NativeWindowHandle -eq $b.Current.NativeWindowHandle }
if (-not $cur) { Say 'FAIL: pin window vanished after thumb click'; exit 4 }
$r3 = $cur[0].Current.BoundingRectangle
Say ('after 2nd-thumb click window = ' + [int]$r3.Width + 'x' + [int]$r3.Height)
Shot ([int]$r3.X) ([int]$r3.Y) ([Math]::Min(1100,[int]$r3.Width)) ([Math]::Min(700,[int]$r3.Height)) 'D:\project\cpp\ltool\build\verify\p2_switched.png'
Say ('captures on disk = ' + (Get-ChildItem "$env:APPDATA\ltool\captures\cap-*.png").Count)
Key 0x1B
Start-Sleep -Milliseconds 400
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; Start-Sleep -Seconds 2 }
Start-Process 'D:\project\cpp\ltool\build\Release\ltool.exe' | Out-Null
Start-Sleep -Seconds 2
Say ('relaunched pid=' + ((Get-Process ltool -ErrorAction SilentlyContinue).Id -join ','))
