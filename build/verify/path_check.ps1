$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
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
function Ch([int[]]$c) { $s = ''; foreach ($x in $c) { $s += [string][char]$x }; return $s }
$S_LANG = Ch @(0x8BED, 0x8A00)
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$root = [System.Windows.Automation.AutomationElement]::RootElement
$PID_TGT = 0
function Say([string]$s) { Write-Host ('| ' + $s) }
function Kids([object]$w, [object]$t) { return $w.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $t))) }
function Foreground([object]$w) {
    for ($i = 0; $i -lt 8; $i++) {
        [W32]::keybd_event(0x12, 0, 0, [IntPtr]::Zero); [W32]::keybd_event(0x12, 0, 2, [IntPtr]::Zero)
        [W32]::SetForegroundWindow([IntPtr]$w.Current.NativeWindowHandle) | Out-Null
        Start-Sleep -Milliseconds 250
        if ([W32]::GetForegroundWindow() -eq [IntPtr]$w.Current.NativeWindowHandle) { return $true }
    }
    return $false
}
function Click([object]$el) {
    $r = $el.Current.BoundingRectangle
    [W32]::SetCursorPos([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2)) | Out-Null
    Start-Sleep -Milliseconds 150
    [W32]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 60
    [W32]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 500
    Say ('  clicked [' + $el.Current.Name + ']')
}

# 1) 把安装目录改成一个不存在的盘符
$cfg = Join-Path $env:APPDATA 'ltool\config.ini'
$backup = (Get-Content $cfg -Raw)
# QSettings 的 INI 里反斜杠是转义写的（D:\\software），照这个格式写才读得对
$text = $backup -replace '(?m)^path=.*$', 'path=Z:\\software'
if ($text -notmatch 'Z:\\\\software') { Say 'WARN: no path= line found, appending'; $text += "`r`n[install]`r`npath=Z:\\software`r`n" }
[IO.File]::WriteAllText($cfg, $text, (New-Object System.Text.UTF8Encoding($false)))
Say ('config install/path -> Z:\software   (drive Z exists = ' + (Test-Path 'Z:\') + ')')

$old = Get-Process ltool -ErrorAction SilentlyContinue
if ($old) { Stop-Process -Id $old.Id -Force; Start-Sleep -Seconds 2 }
$proc = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -ArgumentList '--settings' -PassThru
$PID_TGT = $proc.Id
$win = $null
for ($i = 0; $i -lt 25; $i++) {
    Start-Sleep -Milliseconds 600
    $win = $root.FindFirst($TC::Children, (New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $PID_TGT)))
    if ($win) { break }
}
if (-not $win) { Say 'FAIL: no settings window'; [IO.File]::WriteAllText($cfg, $backup, (New-Object System.Text.UTF8Encoding($false))); exit 2 }
if (-not (Foreground $win)) { Say 'FAIL: cannot take foreground, aborting without clicks'; [IO.File]::WriteAllText($cfg, $backup, (New-Object System.Text.UTF8Encoding($false))); exit 3 }
foreach ($it in (Kids $win $CT::ListItem)) { if ($it.Current.Name -eq $S_LANG) { Click $it; break } }
Start-Sleep -Seconds 2
[void](Foreground $win)
foreach ($it in (Kids $win $CT::ListItem)) { if ($it.Current.Name.ToLower().Contains('rust')) { Click $it; break } }
Start-Sleep -Seconds 2
$btn = $null
foreach ($b in (Kids $win $CT::Button)) { if ($b.Current.Name.Contains('winget')) { $btn = $b; break } }
if (-not $btn) { Say 'FAIL: install button missing' } else { Click $btn }
Start-Sleep -Seconds 4

$line = Select-String -Path (Join-Path $env:APPDATA 'ltool\log.txt') -Pattern 'silent install start' | Select-Object -Last 1
Say ('LOG: ' + $line.Line)
if ($line.Line -like '*--location "C:\software\*') { Say 'PASS: 退到了 C:\software' } else { Say 'FAIL: 没有退到 C 盘' }
Say ('C:\software\rustc 已创建 = ' + (Test-Path 'C:\software\rustc'))

# 2) 复原配置，清掉这次造出来的空目录
[IO.File]::WriteAllText($cfg, $backup, (New-Object System.Text.UTF8Encoding($false)))
Say ('config restored -> ' + ((Select-String -Path $cfg -Pattern '^path=').Line))
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; Start-Sleep -Seconds 2 }
$created = 'C:\software\rustc'
if ((Test-Path $created) -and -not (Get-ChildItem $created -Recurse -File)) {
    Remove-Item $created -Recurse -Force
    if (-not (Get-ChildItem 'C:\software' -Recurse -File)) { Remove-Item 'C:\software' -Recurse -Force }
    Say 'cleaned up C:\software\rustc'
}
Start-Process 'D:\software\ltool\ltool.exe' | Out-Null
Start-Sleep -Seconds 3
Say ('relaunched installed pid=' + ((Get-Process ltool -ErrorAction SilentlyContinue).Id -join ','))
