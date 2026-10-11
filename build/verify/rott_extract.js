// 从 wheel(zip) 里把指定成员解出来存到 build/verify/rott_src/，用 zlib 解 raw deflate
const fs = require('fs');
const zlib = require('zlib');
const path = require('path');
const b = fs.readFileSync('build/verify/rott.whl');
const outDir = 'build/verify/rott_src';
fs.mkdirSync(outDir, { recursive: true });

let eocd = -1;
for (let i = b.length - 22; i >= 0; --i) {
  if (b.readUInt32LE(i) === 0x06054b50) { eocd = i; break; }
}
if (eocd < 0) throw new Error('找不到 EOCD');
let off = b.readUInt32LE(eocd + 16);
const total = b.readUInt16LE(eocd + 10);
let done = 0;
for (let i = 0; i < total; ++i) {
  const method = b.readUInt16LE(off + 10);
  const csize = b.readUInt32LE(off + 20);
  const nl = b.readUInt16LE(off + 28);
  const el = b.readUInt16LE(off + 30);
  const cl = b.readUInt16LE(off + 32);
  const name = b.toString('utf8', off + 46, off + 46 + nl);
  const lho = b.readUInt32LE(off + 42);
  off += 46 + nl + el + cl;
  if (!/\.py$|config\.yaml$/.test(name)) continue;
  const lnl = b.readUInt16LE(lho + 26);
  const lel = b.readUInt16LE(lho + 28);
  const start = lho + 30 + lnl + lel;
  const data = b.subarray(start, start + csize);
  const buf = method === 0 ? Buffer.from(data) : zlib.inflateRawSync(Buffer.from(data));
  const dst = path.join(outDir, path.basename(name));
  fs.writeFileSync(dst, buf);
  done++;
}
console.log('解出', done, '个文件到', outDir);
