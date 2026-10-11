$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$root = $AE::RootElement
$out = New-Object System.Collections.Generic.List[string]
function Say([string]$s) { $out.Add($s) }
function Save-Out { [System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\langui.txt', $out, (New-Object System.Text.UTF8Encoding($true))) }
trap { Save-Out; break }
function Ctrl($el, $type) { return @($el.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $type))) | ForEach-Object { $_ }) }
function Find($list, [string]$re) { return @($list | Where-Object { $_.Current.Name -match $re }) }
function Shot($w, [string]$name) {
  $r = $w.Current.BoundingRectangle
  $bmp = [System.Drawing.Bitmap]::new([int]$r.Width, [int]$r.Height)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen([int]$r.X, [int]$r.Y, 0, 0, $bmp.Size)
  $g.Dispose(); $bmp.Save($name, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
  Say ('saved ' + (Split-Path $name -Leaf))
}
function Inv($e, [string]$what) {
  $p = $e.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
  $p.Invoke(); Say ('invoke ' + $what)
}

$prev = Get-Process ltool -ErrorAction SilentlyContinue
if ($prev) { Stop-Process -Id $prev.Id -Force; Start-Sleep -Seconds 2 }
$proc = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -ArgumentList '--settings' -PassThru
Start-Sleep -Seconds 8
$cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)
$sw = @(Ctrl $root $CT::Window | Where-Object { $_.Current.ProcessId -eq $proc.Id -and $_.Current.Name -match '设置' })
if ($sw.Count -eq 0) { Say 'FAIL: 设置窗口没开'; Save-Out; exit 5 }
Say ('窗口 = ' + $sw[0].Current.Name)
$all = @(Ctrl $sw[0] $CT::Button) + @(Ctrl $sw[0] $CT::Text)
foreach ($e in (Find (Ctrl $sw[0] $CT::Button) '.')) { Say ('  btn [' + $e.Current.Name + ']') }
Shot $sw[0] 'D:\project\cpp\ltool\build\verify\lang_step1.png'
$chip2 = @(Find (Ctrl $sw[0] $CT::Button) '② 安装缺失')
if ($chip2.Count) { Inv $chip2[0] '步骤②'; Start-Sleep -Milliseconds 900; Shot $sw[0] 'D:\project\cpp\ltool\build\verify\lang_step2.png' }
$chip3 = @(Find (Ctrl $sw[0] $CT::Button) '③ 配好环境变量')
if ($chip3.Count) { Inv $chip3[0] '步骤③'; Start-Sleep -Milliseconds 900; Shot $sw[0] 'D:\project\cpp\ltool\build\verify\lang_step3.png' }
$more = @(Find (Ctrl $sw[0] $CT::Button) '关联软件与配置文件')
if ($more.Count) { Inv $more[0] '展开折叠区'; Start-Sleep -Milliseconds 900; Shot $sw[0] 'D:\project\cpp\ltool\build\verify\lang_more.png' }
# 换一门语言（C++）看步骤①
$cnode = @(Find (Ctrl $sw[0] $CT::ListItem) '^C\+\+')
if ($cnode.Count) {
  $p = $cnode[0].GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
  try { $p.Invoke() } catch { $s = $cnode[0].GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern); $s.Select() }
  Say '切到 C++'
  Start-Sleep -Seconds 4
  Shot $sw[0] 'D:\project\cpp\ltool\build\verify\lang_cpp_step1.png'
}
Save-Out
Write-Host 'done'
