const fs = require('fs');
const path = require('path');
const dir = 'D:/project/cpp/ltool/build/verify';
const changed = [];
for (const f of fs.readdirSync(dir)) {
  if (!f.endsWith('.ps1')) continue;
  const p = path.join(dir, f);
  const buf = fs.readFileSync(p);
  if (buf.length >= 3 && buf[0] === 0xEF && buf[1] === 0xBB && buf[2] === 0xBF) continue;
  // 只给真的含非 ASCII 字节的脚本补 BOM
  let nonAscii = false;
  for (const b of buf) if (b > 0x7F) { nonAscii = true; break; }
  if (!nonAscii) continue;
  fs.writeFileSync(p, Buffer.concat([Buffer.from([0xEF, 0xBB, 0xBF]), buf]));
  changed.push(f);
}
console.log('BOM added to: ' + (changed.length ? changed.join(', ') : '(none)'));
