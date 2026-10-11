// 列一个 zip/wheel 的条目（不引第三方库，直接读中央目录）
const fs = require('fs');
const path = process.argv[2];
const only = process.argv[3] ? new RegExp(process.argv[3], 'i') : null;
const b = fs.readFileSync(path);
// 从尾部找 EOCD 签名 0x06054b50
let eocd = -1;
for (let i = b.length - 22; i >= 0 && i > b.length - 66000; --i) {
  if (b.readUInt32LE(i) === 0x06054b50) { eocd = i; break; }
}
if (eocd < 0) throw new Error('不是 zip（找不到 EOCD）');
const total = b.readUInt16LE(eocd + 10);
let off = b.readUInt32LE(eocd + 16);
let sum = 0, n = 0;
for (let i = 0; i < total; ++i) {
  if (b.readUInt32LE(off) !== 0x02014b50) throw new Error('中央目录签名对不上 @' + off);
  const csize = b.readUInt32LE(off + 20);
  const usize = b.readUInt32LE(off + 24);
  const nameLen = b.readUInt16LE(off + 28);
  const extraLen = b.readUInt16LE(off + 30);
  const cmtLen = b.readUInt16LE(off + 32);
  const name = b.toString('utf8', off + 46, off + 46 + nameLen);
  off += 46 + nameLen + extraLen + cmtLen;
  n++;
  sum += usize;
  if (!only || only.test(name)) console.log((usize / 1048576).toFixed(2).padStart(7) + ' MB  ' + name);
}
console.log('--- 条目 ' + n + ' 个，未压缩合计 ' + (sum / 1048576).toFixed(1) + ' MB');
