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
  const i = s.indexOf('TaskScheduler.h');
  console.log('not found; context=' + JSON.stringify(s.slice(i - 12, i + 70)));
}
