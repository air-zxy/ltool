const fs = require('fs');
const p = 'D:/project/cpp/ltool/src/main.cpp';
const NL = String.fromCharCode(13, 10);
let s = fs.readFileSync(p, 'utf8');

function ins(anchor, add, before) {
  const i = s.indexOf(anchor);
  if (i < 0) throw new Error('anchor missing: ' + anchor.slice(0, 40));
  const at = before ? i : i + anchor.length;
  s = s.slice(0, at) + add + s.slice(at);
}

ins('#include "StartupAudit.h"', '#include "SoftwarePage.h"' + NL, true);
ins('#include "TaskScheduler.h"', '#include "UninstallPage.h"' + NL, false);

const fn = [
  '// --uptime：无界面地把"软件"菜单那一行的两个页面各构造一次，量出卡顿在哪一段',
  'int runUiTiming()',
  '{',
  '    const auto one = [](const char* tag, std::function<QWidget*()> make) {',
  '        QElapsedTimer t;',
  '        t.start();',
  '        QWidget* w = make();',
  '        const qint64 ms = t.nsecsElapsed() / 1000000;',
  '        w->deleteLater();',
  '        std::printf("%-16s %8lld ms\\n", tag, ms);',
  '        std::fflush(stdout);',
  '        return ms;',
  '    };',
  '    one("SoftwarePage", [] { return new SoftwarePage; });',
  '    one("UninstallPage", [] { return new UninstallPage; });',
  '    one("SettingsDialog", [] { return new SettingsDialog; });',
  '    qApp->processEvents();',
  '    return 0;',
  '}',
  '',
  '// --fwtry'
].join(NL);

const a = s.indexOf('// --fwtry');
if (a < 0) throw new Error('no fwtry anchor');
s = s.slice(0, a) + fn + s.slice(a + '// --fwtry'.length);

const disp = '    if (args.contains("--uptime"))' + NL + '        return runUiTiming();' + NL + NL;
const dAnchor = '    if (args.contains("--fwtry"))';
const d = s.indexOf(dAnchor);
if (d < 0) throw new Error('no dispatch anchor');
s = s.slice(0, d) + disp + s.slice(d);

if (s.indexOf('#include <functional>') < 0)
  ins('#include <QMap>', '#include <functional>' + NL, true);

fs.writeFileSync(p, s, 'utf8');
console.log('patched, bytes=' + s.length);
