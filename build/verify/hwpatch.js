const fs = require('fs');
const NL = String.fromCharCode(13, 10);
const p = 'D:/project/cpp/ltool/src/SearchWindow.h';
let s = fs.readFileSync(p, 'utf8');
const anchor = '    QElapsedTimer m_showTime;';
const i = s.indexOf(anchor);
if (i < 0) throw new Error('anchor missing');
const lineEnd = s.indexOf(NL, i);
const add = NL + '    QElapsedTimer m_showLatency;   // 从按下呼出到画出来的分段计时（诊断"唤出慢"）' + NL +
            '    bool m_paintLogged = false;';
s = s.slice(0, lineEnd) + add + s.slice(lineEnd);
fs.writeFileSync(p, s, 'utf8');
console.log('header patched');
