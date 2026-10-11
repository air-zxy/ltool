const fs = require('fs');
const NL = String.fromCharCode(13, 10);
const p = 'D:/project/cpp/ltool/src/SearchWindow.h';
let s = fs.readFileSync(p, 'utf8');
const dup = NL + '    QElapsedTimer m_showLatency;   // 从按下呼出到画出来的分段计时（诊断"唤出慢"）' + NL +
            '    bool m_paintLogged = false;';
const first = s.indexOf(dup);
if (first < 0) throw new Error('dup block not found');
const second = s.indexOf(dup, first + dup.length);
if (second < 0) { console.log('no duplicate to remove'); process.exit(0); }
s = s.slice(0, second) + s.slice(second + dup.length);
fs.writeFileSync(p, s, 'utf8');
const count = (s.match(/m_showLatency/g) || []).length;
console.log('duplicate removed; m_showLatency occurrences = ' + count);
