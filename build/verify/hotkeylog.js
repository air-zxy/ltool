// 读 log.txt 里的热键事件，找"两次按压间隔极短且一 show 一 hide"的对：
// 那种情况说明一次按压被吞了（延迟到达），第二次把它收掉了 —— 用户视角就是"按了没出来"。
const fs = require('fs');
const p = process.env.APPDATA + '/ltool/log.txt';
const lines = fs.readFileSync(p, 'latin1').split('\n');
const ev = [];
for (const L of lines) {
  const m = L.match(/^\[(\d{4})-(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d)\] hotkey: (\w+)/);
  if (m) ev.push({ day: Number(m[3]), h: Number(m[4]), mi: Number(m[5]), s: Number(m[6]), kind: m[7], stamp: m[0].slice(1, 20) });
}
const secs = (e) => ((e.day * 24 + e.h) * 60 + e.mi) * 60 + e.s;
let showHide = 0, hideShow = 0, same = 0;
const buckets = { '<=1s': 0, '2-5s': 0, '6-30s': 0 };
for (let i = 1; i < ev.length; i++) {
  const d = secs(ev[i]) - secs(ev[i - 1]);
  if (d > 30) continue;
  const pair = ev[i - 1].kind + '->' + ev[i].kind;
  if (ev[i - 1].kind === ev[i].kind) same++;
  else if (pair === 'show->hide') showHide++;
  else hideShow++;
  if (d <= 1) buckets['<=1s']++;
  else if (d <= 5) buckets['2-5s']++;
  else buckets['6-30s']++;
}
console.log('热键事件总数=' + ev.length);
console.log('30 秒内的相邻对：同种连续=' + same + ' show->hide=' + showHide + ' hide->show=' + hideShow);
console.log('相邻间隔分布:', JSON.stringify(buckets));
console.log('最后 8 个事件:');
for (const e of ev.slice(-8)) console.log('  ' + e.stamp + ' ' + e.kind);
