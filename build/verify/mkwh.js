const fs = require('fs');
const BOM = Buffer.from([0xEF, 0xBB, 0xBF]);
const dir = 'D:\\project\\cpp\\ltool\\build\\verify';
const held = dir + '\\held.bin';
const exe = 'D:\\software\\ltool\\ltool.exe';

function writeBom(path, text) {
  fs.writeFileSync(path, Buffer.concat([BOM, Buffer.from(text, 'utf8')]));
}

const locker = dir + '\\locker3.ps1';
writeBom(locker,
  "$h=[System.IO.File]::Open('" + held + "','Open','ReadWrite','None'); Start-Sleep -Seconds 30" + '\r\n');

const run = dir + '\\whrun.ps1';
writeBom(run, [
  "[System.IO.File]::WriteAllBytes('" + held + "', (New-Object byte[] (1048576)))",
  "$p = Start-Process powershell -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File','"
      + locker + "') -PassThru",
  'Start-Sleep 3',
  "$o = '" + dir + "\\wh.out'",
  "$null = Start-Process -FilePath '" + exe + "' -ArgumentList @('--whohastest', '" + held
      + "') -Wait -NoNewWindow -RedirectStandardOutput $o",
  'Get-Content $o -Encoding UTF8',
  'Stop-Process -Id $p.Id -Force',
  'Remove-Item ' + held + ' -Force -ErrorAction SilentlyContinue',
  ''
].join('\r\n'));
console.log('wrote locker3.ps1 + whrun.ps1');
