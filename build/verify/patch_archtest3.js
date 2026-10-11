const fs = require('fs');
const p = 'D:/project/cpp/ltool/build/verify/archtest.ps1';
let s = fs.readFileSync(p, 'utf8');
const orig = s;

// 1) 加一个单文件 gzip fixture
let hits = 0;
const a1 = "$log.Add('fixtures: '";
s = s.replace(a1, () => { hits++; return '& $seven a -tgzip "$work\\one.gz" "$work\\src\\plain.bin" | Out-Null\n' + a1; });
if (hits !== 1) throw new Error('anchor1 hits=' + hits);

// 2) 加单文件 gz 解压检查
hits = 0;
const a2 = "Run @('--archivetest', 'extract', \"$work\\notarchive.txt\"";
s = s.replace(a2, () => { hits++; return `Run @('--archivetest', 'extract', "$work\\one.gz", "$work\\out_gz") '单文件 gz 解压'\n` + a2; });
if (hits !== 1) throw new Error('anchor2 hits=' + hits);

// 3) 目录清单里加 out_gz
hits = 0;
const a3 = `'out_mine7z', 'out_gbk'`;
s = s.replace(a3, () => { hits++; return `'out_mine7z', 'out_gbk', 'out_gz'`; });
if (hits !== 1) throw new Error('anchor3 hits=' + hits);

if (s.split('out_mine7z').length - 1 !== 2) throw new Error('expected mine.7z roundtrip twice, got ' + (s.split('out_mine7z').length - 1));
fs.writeFileSync(p, s, 'utf8');
console.log('patched, delta=' + (s.length - orig.length));
