# migrate_ltool_data.ps1 -- one-time merge of the OLD data dir (%APPDATA%\ltool) into the
# NEW one (<install dir>\data), for installs whose first migration ran while the bug was in.
#
# Rules (deliberately narrow -- this must never be able to eat a file):
#   1. a file that is MISSING in the target  -> copy it in (sha256-verified, rollback on mismatch)
#   2. a file that exists on BOTH sides      -> NEVER touched, only reported (size/mtime/bytes-equal)
#   3. nothing is ever deleted from either side
# Target dir is taken from the Inno uninstall key's InstallLocation (falls back to -Target).
# ASCII only on purpose: a .ps1 with non-ASCII text and no BOM has blown up in this repo before.
#
#   powershell -NoProfile -File build/verify/migrate_ltool_data.ps1            # dry run (default)
#   powershell -NoProfile -File build/verify/migrate_ltool_data.ps1 -Apply     # actually copy
param(
  [switch]$Apply,
  [string]$Legacy = '',
  [string]$Target = ''
)

$ErrorActionPreference = 'Stop'

function Get-InstallLocation {
  # The uninstall key is Inno's GUID form ("{B7E6F3A1-...}_is1"), NOT "ltool_is1".
  # The first version looked up a hardcoded 'ltool_is1', never found it, and silently fell back
  # to the hardcoded path below -- right by luck. Not finding it must be said out loud.
  $roots = @(
    'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall',
    'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall',
    'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall'
  )
  foreach ($r in $roots) {
    if (-not (Test-Path $r)) { continue }
    foreach ($k in (Get-ChildItem $r -ErrorAction SilentlyContinue)) {
      $v = Get-ItemProperty $k.PSPath -ErrorAction SilentlyContinue
      if ($v.DisplayName -notlike '*ltool*') { continue }
      if ($v.InstallLocation) { return $v.InstallLocation.TrimEnd('\') }
    }
  }
  return ''
}

if (-not $Legacy) { $Legacy = Join-Path $env:APPDATA 'ltool' }
$targetFrom = '-Target argument'
if (-not $Target) {
  $inst = Get-InstallLocation
  if ($inst) { $Target = Join-Path $inst 'data'; $targetFrom = 'uninstall key InstallLocation' }
  else { $Target = 'D:\software\ltool\data'; $targetFrom = 'HARDCODED FALLBACK (no ltool uninstall key found!)' }
}

Write-Output ('mode    : ' + $(if ($Apply) { 'APPLY (copies for real)' } else { 'DRY RUN (nothing written, add -Apply)' }))
Write-Output ('legacy  : ' + $Legacy)
Write-Output ('target  : ' + $Target + '   (source: ' + $targetFrom + ')')
if (-not (Test-Path $Legacy)) { Write-Output 'legacy dir is gone -- nothing to do'; exit 0 }
if (-not (Test-Path $Target)) { Write-Output ('target dir does not exist: ' + $Target); exit 1 }

function Get-Sha($p) {
  $s = [System.IO.File]::OpenRead($p)
  try { (Get-FileHash -InputStream $s -Algorithm SHA256).Hash } finally { $s.Dispose() }
}

$srcFiles = @(Get-ChildItem -LiteralPath $Legacy -Recurse -File -Force)
$copied = 0; $sameName = 0; $identical = 0; $failed = 0; $bytes = 0L
$conflicts = @()
$missing = @()

# Which images does the TARGET's clipboard history reference right now (by image "head":
# abc.png and abc.t120.png share it, so one referenced entry covers its thumbnail too).
# Copied files that nothing references are NOT data loss -- the app's daily sweep will treat
# them as orphans and take them away again. Saying so here is what keeps the next dry run from
# looking like "still 4 missing, the migration never finished".
$referenced = @{}
$cj = Join-Path $Target 'clipboard.json'
if (Test-Path -LiteralPath $cj) {
  try {
    foreach ($it in @(Get-Content -LiteralPath $cj -Raw -Encoding UTF8 | ConvertFrom-Json)) {
      if ($it.image -and $it.path) {
        $head = ([System.IO.Path]::GetFileName([string]$it.path) -split '\.')[0]
        $referenced[$head] = 1
      }
    }
  } catch { Write-Output 'clipboard.json unreadable -- reference split skipped' }
}

foreach ($f in $srcFiles) {
  $rel = $f.FullName.Substring($Legacy.Length).TrimStart('\')
  $dst = Join-Path $Target $rel
  if (Test-Path -LiteralPath $dst) {
    $sameName++
    $a = Get-Sha $f.FullName; $b = Get-Sha $dst
    if ($a -eq $b) { $identical++ } else { $conflicts += ,@($rel, $f.Length, $f.LastWriteTime, (Get-Item -LiteralPath $dst).Length, (Get-Item -LiteralPath $dst).LastWriteTime) }
    continue
  }
  $copied++
  $bytes += $f.Length
  $missing += ,@($rel, $referenced.ContainsKey(($f.Name -split '\.')[0]))
  $verb = if ($Apply) { 'copying   : ' } else { 'would copy: ' }
  Write-Output ($verb + $rel)
  if (-not $Apply) { continue }
  $dir = [System.IO.Path]::GetDirectoryName($dst)
  if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
  Copy-Item -LiteralPath $f.FullName -Destination $dst
  if ((Get-Sha $f.FullName) -ne (Get-Sha $dst)) {
    Remove-Item -LiteralPath $dst -Force     # half-written: roll it back, keep the legacy original
    $copied--; $bytes -= $f.Length; $failed++
    Write-Output ('HASH MISMATCH, rolled back: ' + $rel)
  }
}

Write-Output ('legacy files scanned : ' + $srcFiles.Count)
Write-Output ('missing in target    : ' + $copied + '  (' + [math]::Round($bytes / 1MB, 1) + ' MB)')
Write-Output ('already in target    : ' + $sameName + '  (byte-identical ' + $identical + ', different ' + $conflicts.Count + ')')
Write-Output ('copy failed          : ' + $failed)
$refN = @($missing | Where-Object { $_[1] }).Count
Write-Output ('  of those, referenced by the history now: ' + $refN + ' ; not referenced: ' + ($copied - $refN))
if (($copied - $refN) -gt 0) {
  Write-Output '  (the unreferenced ones are not lost data -- the app calls them orphans and sweeps them again.'
  Write-Output '   That is why a second dry run of this script can still show a few "missing".)'
}
if ($conflicts.Count -gt 0) {
  Write-Output ''
  Write-Output 'These exist on BOTH sides with different bytes. This script does NOT touch them --'
  Write-Output 'the newer one is normally the one in the target (that is where the app writes now).'
  Write-Output ('{0,-44} {1,10} {2,17} {3,10} {4,17}' -f 'file', 'legacy B', 'legacy mtime', 'target B', 'target mtime')
  foreach ($c in ($conflicts | Select-Object -First 40)) {
    Write-Output ('{0,-44} {1,10} {2,17} {3,10} {4,17}' -f $c[0], $c[1], $c[2].ToString('MM-dd HH:mm:ss'), $c[3], $c[4].ToString('MM-dd HH:mm:ss'))
  }
  if ($conflicts.Count -gt 40) { Write-Output ('... and ' + ($conflicts.Count - 40) + ' more') }
}
if (-not $Apply) {
  Write-Output ''
  Write-Output ('dry run: nothing was written. Re-run with -Apply to copy the ' + $copied + ' missing file(s).')
}
exit 0
