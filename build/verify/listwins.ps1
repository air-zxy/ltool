$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$Auto = [System.Windows.Automation.AutomationElement]
$wClose = [string][char]0x5173 + [char]0x95ED

foreach ($p in @(Get-Process ltool -ErrorAction SilentlyContinue)) {
  $c = New-Object System.Windows.Automation.PropertyCondition($Auto::ProcessIdProperty, $p.Id)
  $wins = @($Auto::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $c))
  Write-Output ('pid=' + $p.Id + ' start=' + $p.StartTime + ' windows=' + $wins.Count +
                ' titles=[' + (($wins | ForEach-Object { $_.Current.ClassName + ':' + $_.Current.Name }) -join ' , ') + ']')
}
