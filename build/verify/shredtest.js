const fs = require('fs');
const NL = String.fromCharCode(13, 10);
const p = 'D:/project/cpp/ltool/src/main.cpp';
let s = fs.readFileSync(p, 'utf8');

const anchor = '    // 资源管理器右键菜单进来的动作';
if (s.indexOf(anchor) < 0) throw new Error('anchor missing');
const add = [
  '    // --shredtest <路径> yes：不弹界面直接跑一遍粉碎，用来验证覆写真的发生。',
  '    // 必须带 "yes"，否则只报告目标信息不动手。',
  '    if (args.contains("--shredtest")) {',
  '        const QStringList rest = args.mid(args.indexOf("--shredtest") + 1);',
  '        const QString target = rest.value(0);',
  '        if (rest.value(1) != QLatin1String("yes")) {',
  '            std::printf("shredtest: 没带 yes，只报告不动手 target=%s exists=%d\\n",',
  '                        target.toUtf8().constData(), QFileInfo(target).exists() ? 1 : 0);',
  '            std::fflush(stdout);',
  '            return 0;',
  '        }',
  '        QString err;',
  '        const qint64 erased = FileOps::shred(target, rest.value(2) == QLatin1String("deep"), &err);',
  '        std::printf("shredtest: erased=%lld exists_after=%d err=%s\\n", erased,',
  '                    QFileInfo(target).exists() ? 1 : 0, err.toUtf8().constData());',
  '        std::fflush(stdout);',
  '        return erased < 0 ? 1 : 0;',
  '    }',
  '',
  ''
].join(NL);
s = s.replace(anchor, add + anchor);

if (s.indexOf('#include "FileOps.h"') < 0)
  s = s.replace('#include "FileIndexer.h"', '#include "FileIndexer.h"' + NL + '#include "FileOps.h"');
fs.writeFileSync(p, s, 'utf8');
console.log('shredtest added');
