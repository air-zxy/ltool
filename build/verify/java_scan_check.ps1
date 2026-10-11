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
$S_INST = Ch @(0x5DF2, 0x88C5)             # "installed" marker in the dropdown
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$root = [System.Windows.Automation.AutomationElement]::RootElement
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
    [W32]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 400
    Say ('  clicked [' + $el.Current.Name + ']')
}

$cfg = Join-Path $env:APPDATA 'ltool\config.ini'
$backup = [IO.File]::ReadAllText($cfg)
$enc = New-Object System.Text.UTF8Encoding($false)
# QSettings 的 INI 里反斜杠是转义写的
$text = $backup -replace '(?m)^path=.*$', 'path=C:\\Program Files\\Eclipse Adoptium'
if ($text -notmatch 'Eclipse Adoptium') { Say 'FAIL: could not rewrite install/path'; exit 2 }
[IO.File]::WriteAllText($cfg, $text, $enc)
Say 'install/path -> C:\Program Files\Eclipse Adoptium (JDK 在里面再套一层)'

try {
    $old = Get-Process ltool -ErrorAction SilentlyContinue
    if ($old) { Stop-Process -Id $old.Id -Force; Start-Sleep -Seconds 2 }
    $proc = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -ArgumentList '--settings' -PassThru
    $win = $null
    for ($i = 0; $i -lt 25; $i++) {
        Start-Sleep -Milliseconds 600
        $win = $root.FindFirst($TC::Children, (New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)))
        if ($win) { break }
    }
    if (-not $win) { throw 'no settings window' }
    if (-not (Foreground $win)) { Say 'FAIL: no foreground, no clicks sent'; exit 3 }
    foreach ($it in (Kids $win $CT::ListItem)) { if ($it.Current.Name -eq $S_LANG) { Click $it; break } }
    Start-Sleep -Seconds 2
    [void](Foreground $win)
    foreach ($it in (Kids $win $CT::ListItem)) {
        if ($it.Current.Name.ToLower().Contains('java') -or $it.Current.Name.Contains('JDK')) { Click $it; break }
    }
    Start-Sleep -Seconds 3
    $cb = Kids $win $CT::ComboBox
    if ($cb.Count -eq 0) { Say 'FAIL: no version combobox' } else {
        $pat = $cb[0].GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern)
        $pat.Expand(); Start-Sleep -Milliseconds 800
        $hit = 0
        foreach ($li in (Kids $win $CT::ListItem)) {
            $n = $li.Current.Name
            if ($n.Contains($S_INST)) { Say ('DROPDOWN: ' + $n); $hit++ }
        }
        $pat.Collapse()
        if ($hit -gt 0) { Say 'PASS: 安装目录下两层里的 JDK 被探测到了' } else { Say 'FAIL: 没探测到 JDK' }
    }
    if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; Start-Sleep -Seconds 2 }
} finally {
    [IO.File]::WriteAllText($cfg, $backup, $enc)
    Say ('config restored -> ' + ((Select-String -Path $cfg -Pattern '^path=').Line))
    Start-Process 'D:\software\ltool\ltool.exe' | Out-Null
    Start-Sleep -Seconds 3
    Say ('running: ' + ((Get-Process ltool -ErrorAction SilentlyContinue).Id -join ','))
}
