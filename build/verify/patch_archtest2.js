const fs = require('fs');
const p = 'D:/project/cpp/ltool/build/verify/archtest.ps1';
let s = fs.readFileSync(p, 'utf8');
const orig = s;
let hits = 0;

const a1 = `Run @('--archivetest', 'extract', "$work\\notarchive.txt"`;
s = s.replace(a1, () => { hits++; return `Run @('--archivetest', 'extract', "$work\\mine.7z", "$work\\out_mine7z") '自己产的 7z 再解回来'\n` + a1; });
if (hits !== 1) throw new Error('anchor1 hits=' + hits);

hits = 0;
const a2 = `foreach ($d in @('out_zip', 'out_7z', 'out_tgz', 'out_mine', 'out_gbk'))`;
s = s.replace(a2, () => { hits++; return `foreach ($d in @('out_zip', 'out_7z', 'out_tgz', 'out_mine', 'out_mine7z', 'out_gbk'))`; });
if (hits !== 1) throw new Error('anchor2 hits=' + hits);

fs.writeFileSync(p, s, 'utf8');
console.log('patched, delta=' + (s.length - orig.length));
