$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win32 {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
}
"@
function Ch([int[]]$c) { $s = ''; foreach ($x in $c) { $s += [string][char]$x }; return $s }
# Chinese needles built from code points (keeps this file pure ASCII)
$T_SOFT   = Ch @(0x8F6F, 0x4EF6)              # software menu entry
$T_INST   = Ch @(0x2714)              # check-mark prefix = installed cell
$T_NOT    = Ch @(0x672A, 0x5B89, 0x88C5)      # "not installed"
$T_ONE    = Ch @(0x53EF, 0x4E00, 0x952E, 0x88C5)   # "one-click installable"
$T_MANUAL = Ch @(0x9700, 0x624B, 0x52A8, 0x4E0B, 0x8F7D)  # "manual download needed"
$T_HEAD   = Ch @(0x5DF2, 0x88C5)              # "installed" (inside group header)
$T_OPEN   = Ch @(0x6253, 0x5F00)              # open
$T_VER    = Ch @(0x7248, 0x672C)              # versions
$T_INSTALL= Ch @(0x5B89, 0x88C5)              # install
$T_UPDATE = Ch @(0x66F4, 0x65B0)              # update
$T_REINSTALL = Ch @(0x91CD, 0x88C5)           # reinstall
$T_DL     = Ch @(0x4E0B, 0x8F7D)              # download
$T_SITE   = Ch @(0x5B98, 0x7F51)              # official site
$T_SEARCH = Ch @(0x641C, 0x7D22)              # search
$T_FOOT   = Ch @(0x5171, 0x0020)              # "total " (footer start, unfiltered)
$T_MATCH  = Ch @(0x5339, 0x914d)              # "match" (footer start while filtering)
$T_GAME   = Ch @(0x6E38, 0x620F)              # game (category used as filter needle)

$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$log = [System.Collections.Generic.List[string]]::new()
function Say([string]$s) { $log.Add($s); Write-Host ('| ' + $s) }
function Flush { [System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\soft_group.txt', ($log -join "`r`n"), [System.Text.Encoding]::UTF8) }
function Get-Kids([object]$w, [object]$t) {
    return $w.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $t)))
}
function Invoke-Raise([object]$w) {
    for ($i = 0; $i -lt 8; $i++) {
        [Win32]::keybd_event(0x12, 0, 0, [IntPtr]::Zero)
        [Win32]::keybd_event(0x12, 0, 2, [IntPtr]::Zero)
        [Win32]::SetForegroundWindow([IntPtr]$w.Current.NativeWindowHandle) | Out-Null
        Start-Sleep -Milliseconds 300
        if ([Win32]::GetForegroundWindow() -eq [IntPtr]$w.Current.NativeWindowHandle) { return $true }
    }
    return $false
}
function Invoke-Click([object]$w, [object]$el) {
    if (-not (Invoke-Raise $w)) { throw 'foreground not acquired - stop, do not click blindly' }
    $r = $el.Current.BoundingRectangle
    # guard: the point we are about to click must still belong to our window
    if ($r.X -lt 0 -or $r.Y -lt 0) { throw 'element off screen' }
    [Win32]::SetCursorPos([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2)) | Out-Null
    Start-Sleep -Milliseconds 200
    if ([Win32]::GetForegroundWindow() -ne [IntPtr]$w.Current.NativeWindowHandle) { throw 'lost foreground before click' }
    [Win32]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 60
    [Win32]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 500
}

if (Get-Process ltool -ErrorAction SilentlyContinue) { throw 'another ltool running' }
$proc = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -ArgumentList '--settings' -PassThru
Say ('pid=' + $proc.Id)
$win = $null
for ($i = 0; $i -lt 25; $i++) {
    Start-Sleep -Milliseconds 800
    if ($proc.HasExited) { throw 'exited early' }
    $cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)
    $win = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst($TC::Children, $cond)
    if ($win) { break }
}
if (-not $win) { throw 'no window' }
Start-Sleep -Seconds 2

$soft = $null
foreach ($it in (Get-Kids $win $CT::ListItem)) {
    $r = $it.Current.BoundingRectangle
    Say ('  item [' + $it.Current.Name + '] rect=' + [math]::Round($r.X,0) + ',' + [math]::Round($r.Y,0) + ' ' + [math]::Round($r.Width,0) + 'x' + [math]::Round($r.Height,0))
    if ($it.Current.Name -eq $T_SOFT) { $soft = $it }
}
if (-not $soft) { throw 'software page entry missing' }

# Qt 的 QListWidget 只认合成鼠标点击，而且切页是异步的：点一次、验一次底部统计
$onSoft = $false
for ($try = 1; $try -le 3 -and -not $onSoft; $try++) {
    Invoke-Click $win $soft
    for ($i = 0; $i -lt 12 -and -not $onSoft; $i++) {
        Start-Sleep -Milliseconds 500
        foreach ($t in (Get-Kids $win $CT::Text)) {
            if ($t.Current.Name -and $t.Current.Name.StartsWith($T_FOOT)) { $onSoft = $true; break }
        }
    }
    Say ('click attempt ' + $try + ' -> on software page = ' + $onSoft)
}
if (-not $onSoft) {
    Say 'STUCK ON WRONG PAGE - dumping current text nodes'
    foreach ($t in (Get-Kids $win $CT::Text)) { if ($t.Current.Name) { Say ('  txt: ' + $t.Current.Name) } }
    Flush
    exit 1
}
Start-Sleep -Seconds 2

# ---- group headers ----
Say '--- text nodes on software page ---'
$texts = @(Get-Kids $win $CT::Text)
$heads = @()
$statCells = @()
foreach ($t in $texts) {
    $n = $t.Current.Name
    if (-not $n) { continue }
    if ($n.Contains($T_HEAD) -and $n.Contains('/')) { $heads += $t; Say ('  HEAD: ' + $n) }
    elseif ($n.StartsWith($T_INST) -or $n.StartsWith($T_NOT)) { $statCells += $t }
    elseif ($n.StartsWith($T_FOOT)) { Say ('  FOOT: ' + $n) }
}
Say ('group headers = ' + $heads.Count)
Say ('status cells  = ' + $statCells.Count)

# ---- status column breakdown ----
$cInst = 0; $cOne = 0; $cManual = 0
foreach ($t in $statCells) {
    $n = $t.Current.Name
    if ($n.StartsWith($T_INST)) { $cInst++ }
    elseif ($n.Contains($T_ONE)) { $cOne++ }
    elseif ($n.Contains($T_MANUAL)) { $cManual++ }
    else { Say ('  UNCLASSIFIED: ' + $n) }
}
Say ('status: installed=' + $cInst + ' oneClick=' + $cOne + ' manual=' + $cManual)

# ---- is the status column actually aligned? (right edges) ----
$rights = @{}
foreach ($t in $statCells) {
    $rr = [math]::Round($t.Current.BoundingRectangle.Right, 0)
    $rights[[string]$rr] = $rights[[string]$rr] + 1
}
Say ('status right-edge distinct values = ' + $rights.Count + ' -> ' + (($rights.GetEnumerator() | Sort-Object Name | ForEach-Object { $_.Name + 'x' + $_.Value }) -join ', '))

# ---- buttons present ----
$bn = @{}
foreach ($b in (Get-Kids $win $CT::Button)) {
    $n = $b.Current.Name
    if ($n) { $bn[$n] = $bn[$n] + 1 }
}
Say '--- buttons (name x count) ---'
foreach ($k in ($bn.Keys | Sort-Object)) { Say ('  [' + $k + '] x ' + $bn[$k]) }
Flush

# ---- search filter: type a category name, expect only that group ----
$edits = @(Get-Kids $win $CT::Edit)
Say ('edit controls = ' + $edits.Count)
$box = $null; $bestY = 1e9
foreach ($e in $edits) {
    $r = $e.Current.BoundingRectangle
    Say ('  edit y=' + [math]::Round($r.Y,0) + ' w=' + [math]::Round($r.Width,0) + ' name=[' + $e.Current.Name + ']')
    if ($r.Y -lt $bestY) { $bestY = $r.Y; $box = $e }
}
if (-not $box) { Say 'NO SEARCH BOX FOUND'; Flush; exit 1 }
$vp = $null
try { $vp = $box.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern) } catch { Say ('no ValuePattern: ' + $_.Exception.Message) }
function Report([string]$tag) {
    $ts = @(Get-Kids $win $CT::Text)
    $h = 0; $rw = 0; $ft = ''
    foreach ($t in $ts) {
        $n = $t.Current.Name
        if (-not $n) { continue }
        if ($n.Contains($T_HEAD) -and $n.Contains('/')) { $h++; Say ("  HEAD${tag}: " + $n) }
        elseif ($n.StartsWith($T_INST) -or $n.StartsWith($T_NOT)) { $rw++ }
        elseif ($n.StartsWith($T_FOOT) -or $n.StartsWith($T_MATCH)) { $ft = $n }
    }
    Say ("${tag}: headers=" + $h + " rows=" + $rw + " foot=[" + $ft + "]")
    return $rw
}
Say ('--- filter 1: category "' + $T_GAME + '" ---')
$vp.SetValue($T_GAME)
Start-Sleep -Seconds 2
Report 'F1' | Out-Null

Say '--- filter 2: substring of a software name (zip) ---'
$vp.SetValue('zip')
Start-Sleep -Seconds 2
Report 'F2' | Out-Null

Say '--- filter 3: no match (zzzz) ---'
$vp.SetValue('zzzz')
Start-Sleep -Seconds 2
Report 'F3' | Out-Null

Say '--- filter 4: cleared ---'
$vp.SetValue('')
Start-Sleep -Seconds 2
Report 'F4' | Out-Null

$r = $win.Current.BoundingRectangle
$bmp = New-Object System.Drawing.Bitmap([int]$r.Width, [int]$r.Height)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen([int]$r.X, [int]$r.Y, 0, 0, $bmp.Size)
$g.Dispose()
$bmp.Save('D:\project\cpp\ltool\build\verify\soft_group.png', [System.Drawing.Imaging.ImageFormat]::Png)
Flush
Write-Host 'done'
