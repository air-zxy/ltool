const fs = require('fs');
const NL = String.fromCharCode(13, 10);
const p = 'D:/project/cpp/ltool/src/main.cpp';
let s = fs.readFileSync(p, 'utf8');
const q = String.fromCharCode(34);
const bad = '#include ' + q + 'TaskScheduler.h' + q + '#include ' + q + 'UninstallPage.h' + q;
const good = '#include ' + q + 'TaskScheduler.h' + q + NL + '#include ' + q + 'UninstallPage.h' + q;
if (s.indexOf(bad) >= 0) {
  s = s.replace(bad, good);
  fs.writeFileSync(p, s, 'utf8');
  console.log('fixed merged include line');
} else {
  console.log('merged include not present (already ok?)');
}
const lines = fs.readFileSync(p, 'utf8').split(NL);
console.log('--- head ---');
console.log(lines.slice(0, 34).map((l, i) => (i + 1) + ': ' + l).join(NL));
console.log('--- 240..270 ---');
console.log(lines.slice(239, 270).map((l, i) => (i + 240) + ': ' + l).join(NL));
