const fs = require('fs');
const NL = String.fromCharCode(13, 10);
const p = 'D:/project/cpp/ltool/src/main.cpp';
let s = fs.readFileSync(p, 'utf8');

const fn = [
  '// --adscan：量一次"广告拦截每 5 秒在 GUI 线程上做的事"到底有多贵（无界面）',
  'int runAdScan()',
  '{',
  '    for (int pass = 0; pass < 3; ++pass) {',
  '        QElapsedTimer t;',
  '        t.start();',
  '        const auto hits = AdBlocker::instance().scan();',
  '        std::printf("scan pass %d: %d windows, %lld ms\\n", pass, int(hits.size()),',
  '                    t.nsecsElapsed() / 1000000);',
  '        std::fflush(stdout);',
  '    }',
  '    return 0;',
  '}',
  '',
  'int runUiTiming(const QStringList& args)'
].join(NL);

const anchor = 'int runUiTiming(const QStringList& args)';
if (s.indexOf(anchor) < 0) throw new Error('anchor missing');
s = s.replace(anchor, fn);

const disp = '    if (args.contains("--adscan"))' + NL + '        return runAdScan();' + NL + NL +
             '    if (args.contains("--fwtry"))';
if (s.indexOf('    if (args.contains("--fwtry"))') < 0) throw new Error('dispatch anchor missing');
s = s.replace('    if (args.contains("--fwtry"))', disp);
fs.writeFileSync(p, s, 'utf8');
console.log('adscan diagnostic added');
