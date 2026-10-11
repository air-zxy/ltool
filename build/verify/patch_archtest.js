const fs = require('fs');
const p = 'D:/project/cpp/ltool/build/verify/archtest.ps1';
let s = fs.readFileSync(p, 'utf8');
const orig = s;
let hits = 0;

s = s.replace("$seven a -ttar", () => { hits++; return '$python = (Get-Command python -ErrorAction SilentlyContinue).Source\n& $python "$work\\..\\gbkzip.py" "$work\\gbk.zip" | Out-Null\n$seven a -ttar'; });
if (hits !== 1) throw new Error('anchor1 hits=' + hits);

hits = 0;
const a2 = `Run @('--archivetest', 'extract', "$work\\notarchive.txt"`;
s = s.replace(a2, () => { hits++; return `Run @('--archivetest', 'extract', "$work\\gbk.zip", "$work\\out_gbk") 'GBK 名字的老 zip 解压'\n` + a2; });
if (hits !== 1) throw new Error('anchor2 hits=' + hits);

hits = 0;
const a3 = `foreach ($d in @('out_zip', 'out_7z', 'out_tgz', 'out_mine'))`;
s = s.replace(a3, () => { hits++; return `foreach ($d in @('out_zip', 'out_7z', 'out_tgz', 'out_mine', 'out_gbk'))`; });
if (hits !== 1) throw new Error('anchor3 hits=' + hits);

fs.writeFileSync(p, s, 'utf8');
console.log('patched, delta chars=' + (s.length - orig.length));
