$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$p = $args[0]
$z = [System.IO.Compression.ZipFile]::OpenRead($p)
$rows = @()
foreach ($e in $z.Entries) {
  if ($e.FullName -match '(?i)\.(onnx|txt|json|dll|py)$' -or $e.Length -gt 500000) {
    $rows += [pscustomobject]@{ name = $e.FullName; MB = [math]::Round($e.Length / 1MB, 2) }
  }
}
$rows | Sort-Object -Descending MB | Select-Object -First 25 | ForEach-Object {
  Write-Output ('{0,8:N2} MB  {1}' -f $_.MB, $_.name)
}
$z.Dispose()
