$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$paths = $args
foreach ($p in $paths) {
  Write-Output ('==== ' + $p)
  $z = [System.IO.Compression.ZipFile]::OpenRead($p)
  foreach ($e in $z.Entries) {
    if ($e.FullName -notlike '*.py') {
      Write-Output ('  {0,9:N1} KB  {1}' -f ($e.Length / 1KB), $e.FullName)
    }
  }
  Write-Output ('  条目总数 ' + $z.Entries.Count)
  $z.Dispose()
}
