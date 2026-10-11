# 列一下 HKCU 里 ltool 右键菜单注册了哪几项（路径里有 *，PowerShell 的注册表提供程序
# 会把它当通配符去递归整个 Classes，直接卡死 —— 所以必须用 -LiteralPath）。
# 用法：powershell -File regverbs.ps1
$ErrorActionPreference = 'Continue'
foreach ($p in @('HKCU:\Software\Classes\*\shell\ltool\shell',
                 'HKCU:\Software\Classes\Directory\shell\ltool\shell')) {
    if (Test-Path -LiteralPath $p) {
        $names = @(Get-ChildItem -LiteralPath $p | ForEach-Object { $_.PSChildName })
        Write-Output ($p + '  =>  ' + ($names -join ', '))
    } else {
        Write-Output ($p + '  =>  (没有这一项)')
    }
}
