$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class W32 {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out PT p);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
  [StructLayout(LayoutKind.Sequential)] public struct PT { public int X; public int Y; }
}
"@
$VK_F1 = 0x70; $VK_ESC = 0x1B
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$root = [System.Windows.Automation.AutomationElement]::RootElement
$log = [System.Collections.Generic.List[string]]::new()
function Say([string]$s) { $log.Add($s); Write-Host ('| ' + $s) }
function Flush { [System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\capture.txt', ($log -join "`r`n"), [System.Text.Encoding]::UTF8) }
function Key([byte]$vk) {
    [W32]::keybd_event($vk, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 40
    [W32]::keybd_event($vk, 0, 2, [IntPtr]::Zero); Start-Sleep -Milliseconds 40
}
function PressDown { [W32]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 60 }
function PressUp { [W32]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 200 }
function Goto([int]$x, [int]$y) { [W32]::SetCursorPos($x, $y) | Out-Null; Start-Sleep -Milliseconds 120 }
function Drag([int]$x1, [int]$y1, [int]$x2, [int]$y2, [int]$steps) {
    Goto $x1 $y1; PressDown
    for ($i = 1; $i -le $steps; $i++) {
        Goto ([int]($x1 + ($x2 - $x1) * $i / $steps)) ([int]($y1 + ($y2 - $y1) * $i / $steps))
    }
}
function Shoot([string]$name) {
    $b = New-Object System.Drawing.Bitmap([System.Windows.Forms.Screen]::PrimaryScreen.Bounds.Width),
                                         ([System.Windows.Forms.Screen]::PrimaryScreen.Bounds.Height)
    $g = [System.Drawing.Graphics]::FromImage($b)
    $g.CopyFromScreen(0, 0, 0, 0, $b.Size); $g.Dispose()
    $b.Save('D:\project\cpp\ltool\build\verify\' + $name, [System.Drawing.Imaging.ImageFormat]::Png); $b.Dispose()
    Say ('  saved ' + $name)
}

$old = Get-Process ltool -ErrorAction SilentlyContinue
if ($old) { Say ('closing old pid ' + ($old.Id -join ',')); Stop-Process -Id $old.Id -Force; Start-Sleep -Seconds 2 }
$proc = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -PassThru
Say ('pid=' + $proc.Id)
Start-Sleep -Seconds 4

$scr = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
Say ('screen = ' + $scr.Width + 'x' + $scr.Height)
Key $VK_F1
Start-Sleep -Milliseconds 1500
$cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)
$ov = $null
foreach ($w in $root.FindAll($TC::Children, $cond)) {
    $r = $w.Current.BoundingRectangle
    if ($r.Width -ge ($scr.Width - 2) -and $r.Height -ge ($scr.Height - 2)) { $ov = $w; break }
}
if (-not $ov) { Say 'FAIL: no fullscreen capture overlay after F1'; Shoot 'cap_fail.png'; Flush; exit 2 }
Say 'overlay is up'

function HasOverlay {
    foreach ($w in $root.FindAll($TC::Children, $cond)) {
        $r = $w.Current.BoundingRectangle
        if ($r.Width -ge ($scr.Width - 2) -and $r.Height -ge ($scr.Height - 2)) { return $true }
    }
    return $false
}

# 1) 拖一个选区
$x1 = [int]($scr.Width * 0.30); $y1 = [int]($scr.Height * 0.25)
$x2 = [int]($scr.Width * 0.55); $y2 = [int]($scr.Height * 0.60)
Drag $x1 $y1 $x2 $y2 12
PressUp
Start-Sleep -Milliseconds 600
Shoot 'cap_1_selected.png'
if (-not (HasOverlay)) { Say 'FAIL: overlay gone after first drag'; Flush; exit 4 }

# 从 log 里读出真实选区，照着它定位右下角手柄（不靠猜坐标）
$line = ''
for ($i = 0; $i -lt 12; $i++) {
    Start-Sleep -Milliseconds 300
    $hit = Select-String -Path "$env:APPDATA\ltool\log.txt" -Pattern 'capture: selected' | Select-Object -Last 1
    if ($hit) { $line = $hit.Line; break }
}
Say ('log: ' + $line)
if (-not $line) { Say 'FAIL: no "capture: selected" line in log'; PressUp; Flush; exit 8 }
$m = [regex]::Match($line, 'selected (\d+)x(\d+) at (-?\d+),(-?\d+)')
$sx = [int]$m.Groups[3].Value; $sy = [int]$m.Groups[4].Value
$sw = [int]$m.Groups[1].Value; $sh = [int]$m.Groups[2].Value
$hx = $sx + $sw - 1; $hy = $sy + $sh - 1
Say ('BR handle = ' + $hx + ',' + $hy)
if ($hx -lt 20 -or $hy -lt 20) { Say 'FAIL: bogus handle coords'; PressUp; Flush; exit 9 }

# 2) 抓右下角手柄往外拉（中途拍一张看尺寸标签）
Goto $hx $hy
PressDown
if (-not (HasOverlay)) { Say 'FAIL: overlay closed on handle press'; PressUp; Flush; exit 5 }
Goto ($hx + 60) ($hy + 45)
Goto ($hx + 130) ($hy + 95)
Start-Sleep -Milliseconds 300
Shoot 'cap_2_stretching.png'
Goto ($hx + 170) ($hy + 120)
PressUp
Start-Sleep -Milliseconds 600
if (-not (HasOverlay)) { Say 'FAIL: overlay closed after stretch'; Flush; exit 6 }
Shoot 'cap_3_resized.png'

# 3) 在选区内部拖动 = 整体移动
$mx = $sx + 60; $my = $sy + 60
Drag $mx $my ($mx - 120) ($my - 40) 8
PressUp
Start-Sleep -Milliseconds 500
if (-not (HasOverlay)) { Say 'FAIL: overlay closed after move'; Flush; exit 7 }
Shoot 'cap_4_moved.png'

# 4) 单击选区内部 = 复制并关闭
$cx = $sx + 40; $cy = $sy + 40
Goto $cx $cy
PressDown
PressUp
Start-Sleep -Milliseconds 800
Say ('overlay closed after click-inside: ' + (-not (HasOverlay)))
Key $VK_ESC
Flush
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; Start-Sleep -Seconds 2 }
Start-Process 'D:\software\ltool\ltool.exe' | Out-Null
Start-Sleep -Seconds 3
Say ('relaunched installed pid=' + ((Get-Process ltool -ErrorAction SilentlyContinue).Id -join ','))
Flush
Write-Host 'done'
