const fs = require('fs');
const NL = String.fromCharCode(13, 10);
const p = 'D:/project/cpp/ltool/src/main.cpp';
let s = fs.readFileSync(p, 'utf8');
if (s.indexOf('--archivetest') >= 0) { console.log('already there'); process.exit(0); }

const anchor = '    // --shredtest <路径> yes';
if (s.indexOf(anchor) < 0) throw new Error('anchor missing');
const fn = [
  '    // --archivetest extract <包> <目标目录> / --archivetest make <输入> <输出> [7z]',
  '    // 无界面跑一遍压缩包读写，给回归测试用',
  '    if (args.contains("--archivetest")) {',
  '        const QStringList a = args.mid(args.indexOf("--archivetest") + 1);',
  '        if (a.value(0) == QLatin1String("extract")) {',
  '            const Archive::Result r = Archive::extract(a.value(1), a.value(2), nullptr, nullptr);',
  '            std::printf("extract ok=%d files=%d dirs=%d bytes=%lld target=%s err=%s\\n",',
  '                        r.ok ? 1 : 0, r.files, r.dirs, r.bytes,',
  '                        r.target.toUtf8().constData(), r.error.toUtf8().constData());',
  '        } else {',
  '            const bool seven = a.value(3) == QLatin1String("7z");',
  '            const Archive::Result r = Archive::compress({ a.value(2) }, a.value(1), seven, nullptr, nullptr);',
  '            std::printf("make ok=%d files=%d dirs=%d bytes=%lld target=%s err=%s\\n",',
  '                        r.ok ? 1 : 0, r.files, r.dirs, r.bytes,',
  '                        r.target.toUtf8().constData(), r.error.toUtf8().constData());',
  '        }',
  '        std::fflush(stdout);',
  '        return 0;',
  '    }',
  '',
  ''
].join(NL);
s = s.replace(anchor, fn + anchor);
if (s.indexOf('#include "Archives.h"') < 0)
  s = s.replace('#include "FileOps.h"', '#include "FileOps.h"' + NL + '#include "Archives.h"');
fs.writeFileSync(p, s, 'utf8');
console.log('archivetest added');
