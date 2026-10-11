// 造一份"压得动但不重复到没意义"的伪日志夹具，给端到端计时用：
//   node build/verify/makecorpus.js <目录> <文件数> <每文件KB>
// 和 --archspeed 里的 C++ 生成器同一套 xorshift，保证两边的吞吐可以对照。
const fs = require("fs");
const path = process.argv[2] || "build/verify/arspd/src";
const count = Number(process.argv[3] || 15);
const kb = Number(process.argv[4] || 4096);
fs.mkdirSync(path, { recursive: true });
let s = 88172645463325252n;
const M = 18446744073709551615n; // 2^64-1
for (let f = 0; f < count; f++) {
  const parts = [];
  let n = 0;
  const want = kb * 1024;
  while (n < want) {
    s ^= (s << 13n) & M;
    s >>= 7n;
    s ^= (s << 17n) & M;
    const line = `2026-10-11 12:00:0${Number(s % 9n)} INFO worker-${Number(s % 97n)} processed item ${Number(s % 100000n)} in ${Number(s % 500n)}ms status=OK\n`;
    const b = Buffer.from(line);
    parts.push(b);
    n += b.length;
  }
  fs.writeFileSync(path + "/f" + f + ".log", Buffer.concat(parts).subarray(0, want));
}
const st = fs.statSync(path + "/f0.log");
console.log(`corpus: ${count} x ${kb}KB = ${(count * kb / 1024).toFixed(1)} MB, f0=${st.size}`);
