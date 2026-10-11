const fs = require('fs');
const NL = String.fromCharCode(13, 10);
const p = 'D:/project/cpp/ltool/src/main.cpp';
let s = fs.readFileSync(p, 'utf8');

const anchor = '    one("SettingsDialog", [] { return new SettingsDialog; });';
if (s.indexOf(anchor) < 0) throw new Error('anchor missing');

const add = [
  '    one("SettingsDialog", [] { return new SettingsDialog; });',
  '    // 选中一行时要读防火墙状态：确认这个同步读到底有多贵',
  '    {',
  '        QElapsedTimer t;',
  '        t.start();',
  '        const Firewall::Match m = Firewall::rulesFor(QStringLiteral("D:\\\\\\\\software\\\\\\\\ltool\\\\\\\\ltool.exe"));',
  '        std::printf("rulesFor          %8lld ms (total=%d)\\n", t.nsecsElapsed() / 1000000, m.total);',
  '        std::fflush(stdout);',
  '    }'
].join(NL);

s = s.replace(anchor, add);
fs.writeFileSync(p, s, 'utf8');
console.log('ok');
