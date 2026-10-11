$ErrorActionPreference = 'Stop'
# ASCII-only body. Parses the --regscan dump and re-checks every claimed-missing path.
$src = 'D:/project/cpp/ltool/build/verify/regscan.txt'
$lines = [System.IO.File]::ReadAllLines($src, [System.Text.Encoding]::UTF8)
$per = @{}
$fp = @{}
$out = @()
foreach ($l in $lines) {
    if ($l -notmatch '^\[([^\]]+)\|') { continue }
    $cat = ($l -split '\|')[0].TrimStart('[')
    if ($cat -eq 'PATH 失效项' -or $cat -eq '死盘符' -or $cat -eq '9x 遗留键') { continue }
    $m = [regex]::Matches($l, '不存在：(.+?)(\s+\|\||（|$)')
    foreach ($g in $m) {
        $p = $g.Groups[1].Value.Trim()
        if ($p -notmatch '^[A-Za-z]:\\') { continue }     # ProgID 名之类，不是路径
        if (-not $per.ContainsKey($cat)) { $per[$cat] = 0; $fp[$cat] = 0 }
        $per[$cat] = $per[$cat] + 1
        if (Test-Path -LiteralPath $p) {
            $fp[$cat] = $fp[$cat] + 1
            $out += ('FALSE POSITIVE [' + $cat + '] ' + $p)
        }
    }
}
foreach ($k in $per.Keys) {
    $out += ('cat=' + $k + ' checked=' + $per[$k] + ' stillExists=' + $fp[$k])
}
[System.IO.File]::WriteAllLines('D:/project/cpp/ltool/build/verify/fp.txt', $out,
    (New-Object System.Text.UTF8Encoding($false)))
Write-Output ('wrote fp.txt, lines=' + $out.Count)
