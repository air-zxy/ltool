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
$S_DONE = Ch @(0x5B89, 0x88C5, 0x5B8C, 0x6210)     # "install finished" marker in status line
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$root = [System.Windows.Automation.AutomationElement]::RootElement
function Say([string]$s) { Write-Host ('| ' + $s) }
function Out([string]$s) { $script:out += $s + "`r`n" }
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
    Out ('clicked [' + $el.Current.Name + ']')
}
function StatusText([object]$w) {
    foreach ($t in (Kids $w $CT::Text)) { if ($t.Current.Name.Contains($S_DONE)) { return $t.Current.Name } }
    return ''
}

$script:out = ''
$old = Get-Process ltool -ErrorAction SilentlyContinue
if ($old) { Stop-Process -Id $old.Id -Force; Start-Sleep -Seconds 2 }
$proc = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -ArgumentList '--settings' -PassThru
$win = $null
for ($i = 0; $i -lt 25; $i++) {
    Start-Sleep -Milliseconds 600
    if ($proc.HasExited) { throw 'exited early' }
    $win = $root.FindFirst($TC::Children, (New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)))
    if ($win) { break }
}
if (-not $win) { throw 'no settings window' }
if (-not (Foreground $win)) { Say 'FAIL: no foreground, aborting without clicks'; exit 3 }
foreach ($it in (Kids $win $CT::ListItem)) { if ($it.Current.Name -eq $S_LANG) { Click $it; break } }
Start-Sleep -Seconds 2
[void](Foreground $win)
foreach ($it in (Kids $win $CT::ListItem)) { if ($it.Current.Name.ToLower().Contains('rust')) { Click $it; break } }
Start-Sleep -Seconds 2
$btn = $null
foreach ($b in (Kids $win $CT::Button)) { if ($b.Current.Name.Contains('winget')) { $btn = $b; break } }
if (-not $btn) { Say 'FAIL: install button missing'; exit 4 }
Click $btn
Say 'install kicked off, waiting for the env-setup callback...'
$got = ''
for ($i = 0; $i -lt 30; $i++) {
    Start-Sleep -Seconds 3
    if ($proc.HasExited) { throw ('CRASHED at t+' + (($i + 1) * 3) + 's') }
    $got = StatusText $win
    if ($got) { break }
}
if ($got) { Say ('STATUS: ' + ($got -replace "`r?`n", ' / ')) } else { Say 'FAIL: status line never showed the env-setup result' }
Say ('log tail: ' + ((Select-String -Path (Join-Path $env:APPDATA 'ltool\log.txt') -Pattern 'silent install|install job' | Select-Object -Last 2 | ForEach-Object { $_.Line }) -join ' || '))
$env:PATH = [Environment]::GetEnvironmentVariable('PATH','Machine') + ';' + [Environment]::GetEnvironmentVariable('PATH','User')
foreach ($c in 'cargo','rustc','rustup') {
    $p = Get-Command $c -ErrorAction SilentlyContinue
    Say ($c + ' resolves to ' + $(if ($p) { $p.Source } else { 'NOT FOUND' }))
}
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; Start-Sleep -Seconds 2 }
Start-Process 'D:\software\ltool\ltool.exe' | Out-Null
Start-Sleep -Seconds 3
Say ('relaunched installed pid=' + ((Get-Process ltool -ErrorAction SilentlyContinue).Id -join ','))
[IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\env.txt', $script:out)
