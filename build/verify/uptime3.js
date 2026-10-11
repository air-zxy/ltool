const fs = require('fs');
const NL = String.fromCharCode(13, 10);
const p = 'D:/project/cpp/ltool/src/main.cpp';
let s = fs.readFileSync(p, 'utf8');

s = s.replace('int runUiTiming()' + NL, 'int runUiTiming(const QStringList& args)' + NL);
s = s.replace('        return runUiTiming();', '        return runUiTiming(args);');

const both = '    QWidget* soft = nullptr;';
if (s.indexOf(both) < 0) throw new Error('marker missing');
s = s.replace(both, '    const bool wantSoft = !args.contains("un");' + NL +
                    '    const bool wantUn = !args.contains("soft");' + NL +
                    '    QWidget* soft = nullptr;');

s = s.replace('    timed("SoftwarePage ctor", [&] {' + NL +
              '        soft = new SoftwarePage;' + NL,
              '    timed("SoftwarePage ctor", [&] {' + NL +
              '        if (!wantSoft)' + NL +
              '            return;' + NL +
              '        soft = new SoftwarePage;' + NL);
s = s.replace('    timed("SoftwarePage show", [&] { soft->show(); });',
              '    timed("SoftwarePage show", [&] { if (soft) soft->show(); });');
s = s.replace('    timed("UninstallPage ctor", [&] {' + NL +
              '        un = new UninstallPage;' + NL,
              '    timed("UninstallPage ctor", [&] {' + NL +
              '        if (!wantUn)' + NL +
              '            return;' + NL +
              '        un = new UninstallPage;' + NL);
s = s.replace('    timed("UninstallPage show", [&] { un->show(); });',
              '    timed("UninstallPage show", [&] { if (un) un->show(); });');
fs.writeFileSync(p, s, 'utf8');
console.log('ok');
