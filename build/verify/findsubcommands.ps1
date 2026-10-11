$ErrorActionPreference = 'Continue'
# 在本机所有静态右键项里找 SubCommands（Explorer 靠它建飞出的子菜单），
# 看有没有现成、能用的应用就是这个形状 —— 拿来对照我们的写法。
$found = New-Object System.Collections.Generic.List[string]
$roots = @(
  'Registry::HKEY_CLASSES_ROOT\*\shell',
  'Registry::HKEY_CLASSES_ROOT\Directory\shell',
  'Registry::HKEY_CLASSES_ROOT\Folder\shell',
  'Registry::HKEY_CURRENT_USER\Software\Classes\*\shell',
  'Registry::HKEY_CURRENT_USER\Software\Classes\Directory\shell',
  'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\*\shell'
)
foreach ($r in $roots) {
  if (-not (Test-Path -LiteralPath $r)) { continue }
  foreach ($p in @(Get-ChildItem -LiteralPath $r -ErrorAction SilentlyContinue)) {
    $sub = $null
    try { $sub = (Get-ItemProperty -LiteralPath $p.PSPath -Name SubCommands -ErrorAction SilentlyContinue).SubCommands } catch {}
    $kids = @(Get-ChildItem -LiteralPath $p.PSPath -ErrorAction SilentlyContinue | ForEach-Object { $_.PSChildName })
    $hasShellLevel = $kids -contains 'shell'
    if ($sub -or ($hasShellLevel -and $kids -notcontains 'command')) {
      $inner = @()
      if ($hasShellLevel) {
        $inner = @(Get-ChildItem -LiteralPath (Join-Path $p.PSPath 'shell') -ErrorAction SilentlyContinue | ForEach-Object { $_.PSChildName })
      }
      $found.Add(('{0}' -f $p.Name))
      $found.Add(('    SubCommands=[{0}]' -f $sub))
      $found.Add(('    有 shell 层={0}  子项={1}' -f $hasShellLevel, ($inner -join ',')))
      $found.Add(('    默认值=[{0}]  值名={1}' -f (Get-ItemProperty -LiteralPath $p.PSPath -Name '(default)' -ErrorAction SilentlyContinue).'(default)', ((Get-Item $p.PSPath).GetValueNames() -join ',')))
    }
  }
}
if ($found.Count -eq 0) { Write-Output '本机没找到任何用 SubCommands 或 shell 嵌套层的右键项' }
$found | ForEach-Object { Write-Output $_ }
