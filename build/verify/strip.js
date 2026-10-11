const fs = require('fs');
const p = 'D:/project/cpp/ltool/src/main.cpp';
let s = fs.readFileSync(p, 'utf8');
const NL = '\r\n';
const start = s.indexOf('// --pintest');
const endRe = /    QTimer::singleShot\(4500, \[\] \{ QApplication::quit\(\); \}\);\r\n    return QApplication::exec\(\);\r\n\}\r\n\r\n/;
const m = endRe.exec(s.slice(start));
if (start < 0 || !m) throw new Error('boundaries not found');
const end = start + m.index + m[0].length;
const removed = s.slice(start, end);
if ((removed.match(/runPinTest/g) || []).length !== 1) throw new Error('boundary ' + removed.length);
s = s.slice(0, start) + s.slice(end);

const disp = '    if (args.contains("--pintest"))' + NL + '        return runPinTest();' + NL + NL;
if (!s.includes(disp)) throw new Error('disp missing');
s = s.replace(disp, '');

for (const inc of ['#include "PinWindow.h"' + NL, '#include <QProcess>' + NL, '#include <QMouseEvent>' + NL, '#include <QScreen>' + NL]) {
  if (!s.includes(inc)) throw new Error('inc missing ' + JSON.stringify(inc));
  s = s.replace(inc, '');
}
if (s.includes('pintest') || s.includes('PinWindow')) throw new Error('leftover');
fs.writeFileSync(p, s);
console.log('ok, removed', removed.split(NL).length, 'lines');
