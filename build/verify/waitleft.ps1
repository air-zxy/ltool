$ErrorActionPreference = 'Stop'
$p = Get-Process ltool -ErrorAction SilentlyContinue
if ($p) {
    Write-Output ("waiting on leftover pid " + $p.Id)
    $p.WaitForExit()
    Write-Output ("leftover exit=0x" + ('{0:X8}' -f $p.ExitCode))
} else {
    Write-Output 'no leftover'
}
