$ErrorActionPreference = 'Continue'
# 在本机现有的右键注册表里找"级联子菜单"的真实形状：父键下面还有一层 shell
$roots = @(
  'Registry::HKEY_CLASSES_ROOT\*\shell',
  'Registry::HKEY_CLASSES_ROOT\Directory\shell',
  'Registry::HKEY_CURRENT_USER\Software\Classes\*\shell',
  'Registry::HKEY_CURRENT_USER\Software\Classes\Directory\shell'
)
foreach ($r in $roots) {
  if (-not (Test-Path -LiteralPath $r)) { continue }
  foreach ($p in Get-ChildItem -LiteralPath $r -ErrorAction SilentlyContinue) {
    $kids = @(Get-ChildItem -LiteralPath $p.PSPath -ErrorAction SilentlyContinue)
    $names = $kids | ForEach-Object { $_.PSChildName }
    $hasShell = $names -contains 'shell'
    $hasCommand = $names -contains 'command'
    if ($hasShell) {
      Write-Output ('级联: ' + $p.Name + '   (子键: ' + ($names -join ',') + ')')
      foreach ($g in $kids) {
        if ($g.PSChildName -ne 'shell') { continue }
        foreach ($c in @(Get-ChildItem -LiteralPath $g.PSPath -ErrorAction SilentlyContinue)) {
          Write-Output ('      -> ' + $c.PSChildName + '  标签=[' + (Get-ItemProperty -LiteralPath $c.PSPath -Name '(default)' -ErrorAction SilentlyContinue).'(default)' + ']')
        }
      }
    } elseif (-not $hasCommand) {
      Write-Output ('父键但没有 command 也没有 shell: ' + $p.Name + '  (子键: ' + ($names -join ',') + ')')
    }
  }
}
