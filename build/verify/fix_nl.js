const fs = require('fs');
const BS = String.fromCharCode(92);            // backslash
const NL = String.fromCharCode(10);            // real newline (what the broken file contains)
const ESC_N = BS + 'n';                        // the C++ escape we want instead

const p = 'D:/project/cpp/ltool/src/StartupPage.cpp';
let s = fs.readFileSync(p, 'utf8');

function lit(parts) { return 'QString("' + parts.join(NL) + '")'; }
function fixed(parts) { return 'QString("' + parts.join(ESC_N) + '")'; }

const fixes = [
  [lit(['%1', '位置：%2', '命令：%3', '说明：%4']),
   fixed(['%1', '位置：%2', '命令：%3', '说明：%4'])],
  [lit(['· %1', '  来源：%2  状态：%3', '  位置：%4', '  命令：%5', '  签名：%6', '', '']),
   fixed(['· %1', '  来源：%2  状态：%3', '  位置：%4', '  命令：%5', '  签名：%6', '', ''])],
];

for (const [from, to] of fixes) {
  if (!s.includes(from)) throw new Error('anchor missing: ' + JSON.stringify(from.slice(0, 30)));
  s = s.split(from).join(to);
}
if (s.includes(BS + BS + 'n')) throw new Error('double backslash n left');
fs.writeFileSync(p, s);
console.log('ok');
