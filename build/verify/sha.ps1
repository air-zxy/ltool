$ErrorActionPreference = 'Stop'

function Show-Exe($path) {
    $fs = [System.IO.File]::OpenRead($path)
    $br = New-Object System.IO.BinaryReader($fs)
    $fs.Position = 0x3C
    $peOff = $br.ReadInt32()
    $fs.Position = $peOff + 6
    $ts = $br.ReadUInt32()
    $br.Close()
    $fs.Close()
    $h = (Get-FileHash -Algorithm SHA256 $path).Hash
    $len = (Get-Item $path).Length
    Write-Output ("{0} peTs={1:X8} size={2} sha={3}" -f $path, [uint32]$ts, $len, $h.Substring(0, 16))
}

Show-Exe 'D:/project/cpp/ltool/build/Release/ltool.exe'
Show-Exe 'D:/software/ltool/ltool.exe'
