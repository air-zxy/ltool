const fs = require('fs');
const NL = String.fromCharCode(13, 10);
const p = 'D:/project/cpp/ltool/src/SearchWindow.h';
let s = fs.readFileSync(p, 'utf8');

const anchor = '    void startInitialScan();';
if (s.indexOf(anchor) < 0) throw new Error('startInitialScan missing');
if (s.indexOf('void warmUp();') < 0)
  s = s.replace(anchor, anchor + NL + '    void warmUp();   // 启动后先映射+绘制一遍，别把这份开销留给第一次按热键');

const memAnchor = '    bool m_paintLogged = false;';
if (s.indexOf(memAnchor) < 0) throw new Error('m_paintLogged missing');
if (s.indexOf('m_warmed') < 0)
  s = s.replace(memAnchor, memAnchor + NL + '    bool m_warmed = false;         // 预热只做一次');

fs.writeFileSync(p, s, 'utf8');
console.log('header ok: warmUp=' + (s.match(/void warmUp\(\);/g) || []).length +
            ' m_warmed=' + (s.match(/m_warmed/g) || []).length);
