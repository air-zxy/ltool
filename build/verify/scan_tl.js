const fs = require('fs');
const rl = require('readline');
const path = process.env.USERPROFILE + '/.qoder-cn/projects/D--project-cpp-ltool/<会话id>.jsonl';
const inp = fs.createReadStream(path, { encoding: 'utf8' });
const rl2 = rl.createInterface({ input: inp, crlfDelay: Infinity });
let n = 0;
const hits = [];
rl2.on('line', (l) => {
  n++;
  if (l.indexOf('SoftwarePage::rebuild') < 0) return;
  let obj;
  try { obj = JSON.parse(l); } catch (e) { return; }
  const txt = JSON.stringify(obj);
  const len = txt.length;
  hits.push({ n, len, type: obj.type, role: (obj.message && obj.message.role) || '' });
});
rl2.on('close', () => {
  console.log('lines=' + n + ' candidates=' + hits.length);
  hits.sort((a, b) => b.len - a.len);
  hits.slice(0, 12).forEach((h) => console.log(JSON.stringify(h)));
});
