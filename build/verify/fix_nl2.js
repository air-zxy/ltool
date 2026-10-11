const fs = require('fs');
const BS = String.fromCharCode(92);
const NL = String.fromCharCode(10);
const p = 'D:/project/cpp/ltool/src/StartupPage.cpp';
let s = fs.readFileSync(p, 'utf8');
const broken = 'QStringLiteral("ltool 开机启动分析报告' + NL + NL + '");';
const good = 'QStringLiteral("ltool 开机启动分析报告' + BS + 'n' + BS + 'n")' + ';';
if (!s.includes(broken)) throw new Error('anchor missing');
s = s.replace(broken, good);
// 全文件复查：字符串字面量里不该再有真换行
const bad = s.split(/\r?\n/).filter(l => (l.match(/"/g) || []).length % 2 === 1 && l.includes('"') && !l.endsWith(';') && !l.includes('arg('));
fs.writeFileSync(p, s);
console.log('ok; suspicious lines:', bad.length);
