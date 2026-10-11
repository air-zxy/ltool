const fs = require('fs');
const NL = String.fromCharCode(13, 10);
const p = 'D:/project/cpp/ltool/src/main.cpp';
let s = fs.readFileSync(p, 'utf8');

const start = s.indexOf('int runUiTiming()');
if (start < 0) throw new Error('runUiTiming missing');
const end = s.indexOf(NL + '}' + NL, start);
if (end < 0) throw new Error('end missing');
const oldFn = s.slice(start, end + 3);

const lines = [
  'int runUiTiming()',
  '{',
  '    auto timed = [](const char* tag, std::function<void()> f) {',
  '        QElapsedTimer t;',
  '        t.start();',
  '        f();',
  '        std::printf("%-20s %8lld ms\\n", tag, t.nsecsElapsed() / 1000000);',
  '        std::fflush(stdout);',
  '    };',
  '    QWidget* soft = nullptr;',
  '    QWidget* un = nullptr;',
  '    timed("SoftwarePage ctor", [&] {',
  '        soft = new SoftwarePage;',
  '        soft->setAttribute(Qt::WA_DontShowOnScreen);',
  '    });',
  '    timed("SoftwarePage show", [&] { soft->show(); });',
  '    timed("UninstallPage ctor", [&] {',
  '        un = new UninstallPage;',
  '        un->setAttribute(Qt::WA_DontShowOnScreen);',
  '    });',
  '    timed("UninstallPage show", [&] { un->show(); });',
  '    {',
  '        QElapsedTimer t;',
  '        t.start();',
  '        QEventLoop loop;',
  '        QTimer::singleShot(2500, &loop, &QEventLoop::quit);',
  '        loop.exec();',
  '        std::printf("%-20s %8lld ms (slices drained)\\n", "event loop", t.nsecsElapsed() / 1000000);',
  '    }',
  '    const Firewall::Match m = Firewall::rulesFor(QStringLiteral("D:\\\\software\\\\ltool\\\\ltool.exe"));',
  '    std::printf("rulesFor           %8d rules\\n", m.total);',
  '    std::fflush(stdout);',
  '    delete soft;',
  '    delete un;',
  '    return 0;',
  '}',
  ''
].join(NL);

s = s.slice(0, start) + lines + s.slice(end + 3);
if (s.indexOf('#include <QEventLoop>') < 0)
  s = s.replace('#include <QElapsedTimer>', '#include <QElapsedTimer>' + NL + '#include <QEventLoop>');
fs.writeFileSync(p, s, 'utf8');
console.log('replaced runUiTiming; old len=' + oldFn.length);
