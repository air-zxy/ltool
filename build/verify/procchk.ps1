$ErrorActionPreference = 'Continue'
Get-CimInstance Win32_Process | Where-Object { $PSItem.Name -like '*setup*' -or $PSItem.Name -like 'ltool*' } | ForEach-Object {
  Write-Output ("{0}  PID={1}  cmd={2}" -f $PSItem.Name, $PSItem.ProcessId, $PSItem.CommandLine)
}
