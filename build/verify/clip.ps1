$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
function Ch([int[]]$c) { $s = ''; foreach ($x in $c) { $s += [string][char]$x }; return $s }
$log = [System.Collections.Generic.List[string]]::new()
function Say([string]$s) { $log.Add($s); Write-Host ('| ' + $s) }
function Flush { [System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\clip.txt', ($log -join "`n"), (New-Object System.Text.UTF8Encoding($false))) }
function Wait-Clip([scriptblock]$cond, [int]$secs) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $secs) {
        Start-Sleep -Milliseconds 400
        if (& $cond) { return [math]::Round($sw.Elapsed.TotalSeconds, 1) }
    }
    return -1
}
function Set-Cfg([string]$section, [string]$key, [string]$val) {
    $path = Join-Path $env:APPDATA 'ltool\config.ini'
    $lines = @(Get-Content $path -ErrorAction SilentlyContinue)
    $out = [System.Collections.Generic.List[string]]::new()
    $secIdx = -1
    for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i].Trim() -eq ('[' + $section + ']')) { $secIdx = $i; break } }
    if ($secIdx -lt 0) {
        foreach ($l in $lines) { $out.Add($l) }
        $out.Add(''); $out.Add('[' + $section + ']'); $out.Add("$key=$val")
    } else {
        $end = $lines.Count
        for ($i = $secIdx + 1; $i -lt $lines.Count; $i++) { if ($lines[$i].Trim().StartsWith('[')) { $end = $i; break } }
        $hit = $false
        for ($i = 0; $i -lt $lines.Count; $i++) {
            if ($i -gt $secIdx -and $i -lt $end -and $lines[$i] -match ('^\s*' + [regex]::Escape($key) + '\s*=')) { $out.Add("$key=$val"); $hit = $true; continue }
            $out.Add($lines[$i])
        }
        if (-not $hit) { $out.Insert($secIdx + 1, "$key=$val") }
    }
    [System.IO.File]::WriteAllLines($path, $out, (New-Object System.Text.UTF8Encoding($false)))
}

$dir = Join-Path $env:APPDATA 'ltool'
$cfg = Join-Path $dir 'config.ini'
$store = Join-Path $dir 'clipboard.json'
$imgDir = Join-Path $dir 'clipimg'
$lg = Join-Path $dir 'log.txt'

# 备份用户剪贴板（只备文本，测完还原）
$savedClip = ''
try { if ([Windows.Forms.Clipboard]::ContainsText()) { $savedClip = [Windows.Forms.Clipboard]::GetText() } } catch { }
Say ('saved user clipboard text length = ' + $savedClip.Length)

Copy-Item $cfg "$cfg.clipbak" -Force
$script:logBase = @(Get-Content $lg -ErrorAction SilentlyContinue).Count
Stop-Process -Name ltool -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
Remove-Item $store -Force -ErrorAction SilentlyContinue
if (Test-Path $imgDir) { Remove-Item "$imgDir\*.png" -Force -ErrorAction SilentlyContinue }

# 1) default-off: start with the switch absent/false, copy something, expect NOTHING recorded
Set-Cfg 'clipboard' 'enable' 'false'
$p = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -PassThru
Start-Sleep -Seconds 5
'默认关闭测试' | Set-Clipboard
$t = Wait-Clip { Test-Path $store } 6
Say ('case1 disabled: clipboard.json appeared = ' + (Test-Path $store) + ' (expect False)')
Stop-Process -Name ltool -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2

# 2) enabled: text capture + dedupe + ordering
Set-Cfg 'clipboard' 'enable' 'true'
$p2 = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -PassThru
Start-Sleep -Seconds 5
$alpha = Ch @(0x7b2c, 0x4e00, 0x6bb5)     # 第一段
$beta  = Ch @(0x7b2c, 0x4e8c, 0x6bb5)     # 第二段
"$alpha-123" | Set-Clipboard
$t1 = Wait-Clip { (Get-Content $store -Raw -ErrorAction SilentlyContinue) -match [regex]::Escape($alpha) } 10
Say ('case2 first text captured after ' + $t1 + 's')
"$beta-456" | Set-Clipboard
$t2 = Wait-Clip { (Get-Content $store -Raw -ErrorAction SilentlyContinue) -match [regex]::Escape($beta) } 10
Say ('case2 second text captured after ' + $t2 + 's')
"$alpha-123" | Set-Clipboard
Start-Sleep -Seconds 4
$arr = @()
if (Test-Path $store) { $arr = @(ConvertFrom-Json -InputObject ([IO.File]::ReadAllText($store, [Text.Encoding]::UTF8))) }
Say ('case2 items = ' + $arr.Count + ' (expect 2, dedupe must not add a third)')
Say ('case2 top item is the re-copied one = ' + ($arr[0].text -match [regex]::Escape($alpha)))
Say ('case2 raw = ' + ((Get-Content $store -Raw) -replace '\s+', ' ' ))

# 3) image capture
$bmp = New-Object System.Drawing.Bitmap(64, 48)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::Coral)
$g.Dispose()
[System.Windows.Forms.Clipboard]::SetImage($bmp)
$t3 = Wait-Clip { @(Get-ChildItem $imgDir -Filter *.png -ErrorAction SilentlyContinue).Count -gt 0 } 10
$arr2 = @(ConvertFrom-Json -InputObject ([IO.File]::ReadAllText($store, [Text.Encoding]::UTF8)))
$imgs = @($arr2 | Where-Object { $PSItem.image -eq $true })
Say ('case3 image captured after ' + $t3 + 's · image items = ' + $imgs.Count)
if ($imgs.Count -gt 0) {
    Say ('case3 item = w=' + $imgs[0].w + ' h=' + $imgs[0].h + ' bytes=' + $imgs[0].bytes + ' path=' + $imgs[0].path)
    Say ('case3 file exists = ' + (Test-Path $imgs[0].path))
}
$bmp.Dispose()
Flush

# 4) a >256KB text must be skipped
$big = ('x' * 300000)
Set-Clipboard -Value $big
Start-Sleep -Seconds 5
$arr3 = @(ConvertFrom-Json -InputObject ([IO.File]::ReadAllText($store, [Text.Encoding]::UTF8)))
Say ('case4 items after a 300KB copy = ' + $arr3.Count + ' (expect unchanged: too-large text is skipped)')
$hasBig = @($arr3 | Where-Object { $PSItem.text -and $PSItem.text.Length -gt 100000 }).Count
Say ('case4 big text stored = ' + ($hasBig -gt 0) + ' (expect False)')

# --- cleanup: disable, wipe test history, restore the user's clipboard and the installed app ---
Stop-Process -Name ltool -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
if ($savedClip.Length -gt 0) { Set-Clipboard -Value $savedClip }
Move-Item "$cfg.clipbak" $cfg -Force
Remove-Item $store -Force -ErrorAction SilentlyContinue
if (Test-Path $imgDir) { Remove-Item "$imgDir\*.png" -Force -ErrorAction SilentlyContinue }
Say 'cleanup: config restored, test history + images removed, clipboard put back'
Start-Process -FilePath 'D:\software\ltool\ltool.exe'
Say 'restarted the installed ltool'
Flush
Write-Host 'done'
