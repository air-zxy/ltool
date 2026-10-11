$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$Auto = [System.Windows.Automation.AutomationElement]
$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
$img = 'D:\project\cpp\ltool\build\verify\pintest\base.png'
$p = Start-Process -FilePath $exe -ArgumentList @('--pinshot', $img) -PassThru
Start-Sleep -Seconds 3
$cond = New-Object System.Windows.Automation.PropertyCondition($Auto::ProcessIdProperty, $p.Id)
$win = $null
foreach ($w in $Auto::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $cond)) {
  if ($w.Current.ClassName -eq 'PinWindow') { $win = $w; break }
}
if (-not $win) { Write-Output 'NO-WINDOW'; Stop-Process -Id $p.Id -Force; exit 1 }
$out = New-Object System.Collections.Generic.List[string]
$wr = $win.Current.BoundingRectangle
$out.Add("window $($wr.X),$($wr.Y) $($wr.Width)x$($wr.Height)")
foreach ($e in $win.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)) {
  $r = $e.Current.BoundingRectangle
  $out.Add(('{0,-28} class={1,-16} name=[{2}] off={3} rect={4},{5} {6}x{7}' -f `
            $e.Current.ControlType.ProgrammaticName, $e.Current.ClassName, $e.Current.Name, $e.Current.IsOffscreen, `
            [int]$r.X, [int]$r.Y, [int]$r.Width, [int]$r.Height))
}
[System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\pinui.txt', $out, (New-Object System.Text.UTF8Encoding($true)))
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
Write-Output ('rows=' + $out.Count)
