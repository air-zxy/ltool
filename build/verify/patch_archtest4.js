const fs = require('fs');
const p = 'D:/project/cpp/ltool/build/verify/archtest.ps1';
let s = fs.readFileSync(p, 'utf8');
const orig = s;

// 1) 造一个真实存在的纯文本 fixture
let hits = 0;
const a1 = "$log.Add('fixtures: '";
s = s.replace(a1, () => { hits++; return "Set-Content -Path \"$work\\notarchive.txt\" -Value ('just text, not an archive') -Encoding ASCII\n" + a1; });
if (hits !== 1) throw new Error('anchor1 hits=' + hits);

// 2) 把"非压缩包"那条改成：先探测（应为 0），再硬解（raw 兜底会当成一个条目，界面上走不到）
hits = 0;
const a2 = "Run @('--archivetest', 'extract', \"$work\\notarchive.txt\", \"$work\\out_bad\") '非压缩包（应失败）'";
s = s.replace(a2, () => { hits++; return [
  "Run @('--archivetest', 'probe', \"$work\\notarchive.txt\") '探测：纯文本（应 looksLikeArchive=0）'",
  "Run @('--archivetest', 'probe', \"$work\\pack.zip\") '探测：zip（应 1）'",
  "Run @('--archivetest', 'probe', \"$work\\one.gz\") '探测：单文件 gz（应 1）'",
  "Run @('--archivetest', 'extract', \"$work\\notarchive.txt\", \"$work\\out_bad\") '硬解纯文本（右键走不到这一步）'"
].join('\n'); });
if (hits !== 1) throw new Error('anchor2 hits=' + hits);

fs.writeFileSync(p, s, 'utf8');
console.log('patched, delta=' + (s.length - orig.length));
