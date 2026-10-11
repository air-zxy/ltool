const fs = require('fs');
const p = 'D:/project/cpp/ltool/build/verify/fwrefactor2.js';
let s = fs.readFileSync(p, 'utf8');
const lines = s.split(/\r?\n/);
let fixed = 0;
for (let i = 0; i < lines.length; i++) {
  if (lines[i].includes('QLatin1Char(') && lines[i].includes('.arg(ruleName));')) {
    const indent = lines[i].match(/^\s*/)[0];
    lines[i] = indent + "'                   .arg(quint32(VAR), 8, 16, QLatin1Char(TOK)).arg(ruleName));',";
    lines[i] = lines[i].replace('VAR', lines[i - 1].includes('COM add') ? 'addHr' : 'hr');
    fixed++;
  }
}
if (fixed !== 2) throw new Error('fixed ' + fixed);
s = lines.join('\n');
s = s.replace("const j = lines => lines.join(NL);",
              "const j = lines => lines.join(NL);\nconst AP = String.fromCharCode(39);");
s = s.replace('const body = j([', 'const bodyRaw = j([');
s = s.replace('s = s.slice(0, start) + body + s.slice(end);',
              "const body = bodyRaw.split('TOK').join(AP + '0' + AP);\ns = s.slice(0, start) + body + s.slice(end);");
fs.writeFileSync(p, s);
console.log('fixer ok');
