# crashsig.ps1 -- list ltool crash signatures (module + offset + code) from the Application log.
# ASCII only on purpose: a .ps1 with non-ASCII text and no BOM has blown up in this repo before.
param([int]$Days = 7)
$ErrorActionPreference = 'SilentlyContinue'
$ev = Get-WinEvent -FilterHashtable @{ LogName = 'Application'; Id = 1001; StartTime = (Get-Date).AddDays(-$Days) }
foreach ($e in $ev) {
  if ($e.Message -notmatch 'ltool') { continue }
  $p = @{}
  foreach ($m in [regex]::Matches($e.Message, 'P(\d+):\s*(\S+)')) { $p[$m.Groups[1].Value] = $m.Groups[2].Value }
  '{0}  app={1} faulting={2} ver={3} offset={4} code={5} addr={6}' -f `
    $e.TimeCreated.ToString('MM-dd HH:mm:ss'), $p['1'], $p['4'], $p['5'], $p['7'], $p['8'], $p['9']
}
