# Sync the freshly built plugin dlls into the installed tree (a core relink changes every
# plugin's hash, since they all link ltoolcore). Stop -> copy -> start, because a running
# instance keeps the old dlls mapped and would go on answering with them.
# ASCII only: a .ps1 with Chinese text and no BOM makes PowerShell abort the whole file.
$ErrorActionPreference = 'Stop'
$src = 'D:\project\cpp\ltool\build\Release\ltool-plugins'
$dst = 'D:\software\ltool\ltool-plugins'
$procs = @(Get-Process ltool -ErrorAction SilentlyContinue)
Write-Output ("stopping " + $procs.Count + " instance(s)")
$procs | Stop-Process -Force
Start-Sleep -Milliseconds 900
$n = 0
foreach ($f in Get-ChildItem "$src\*.dll") {
  Copy-Item $f.FullName (Join-Path $dst $f.Name) -Force
  $n = $n + 1
}
$dstCount = @(Get-ChildItem "$dst\*.dll").Count
$match = 0
foreach ($f in Get-ChildItem "$dst\*.dll") {
  $b = Join-Path $src $f.Name
  if (Test-Path $b) {
    if ((Get-FileHash $f.FullName -Algorithm SHA256).Hash -eq (Get-FileHash $b -Algorithm SHA256).Hash) { $match = $match + 1 }
  }
}
Write-Output ("copied " + $n + " -> installed " + $dstCount + " dlls, hash-matching build: " + $match)
Start-Process -FilePath 'D:\software\ltool\ltool.exe'
Start-Sleep -Seconds 2
$after = @(Get-Process ltool -ErrorAction SilentlyContinue)
foreach ($q in $after) { Write-Output ("relaunched pid=" + $q.Id + " since=" + $q.StartTime.ToString("HH:mm:ss")) }
