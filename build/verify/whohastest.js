const fs = require('fs');
const NL = String.fromCharCode(13, 10);
const p = 'D:/project/cpp/ltool/src/main.cpp';
let s = fs.readFileSync(p, 'utf8');
const anchor = '    // --shredtest <路径> yes';
if (s.indexOf(anchor) < 0) throw new Error('anchor missing');
const add = [
  '    // --whohastest <路径>：无界面跑一遍句柄枚举，把每一步的结果打出来',
  '    if (args.contains("--whohastest")) {',
  '        const QString target = args.mid(args.indexOf("--whohastest") + 1).value(0);',
  '        QString err;',
  '        const auto holders = FileOps::whoHas(target, &err);',
  '        std::printf("whohastest: target=%s holders=%d err=%s\\n", target.toUtf8().constData(),',
  '                    int(holders.size()), err.toUtf8().constData());',
  '        for (const FileOps::Holder& h : holders)',
  '            std::printf("  pid=%llu %s handles=%d\\n", h.pid, h.processName.toUtf8().constData(), h.handles);',
  '        std::fflush(stdout);',
  '        return 0;',
  '    }',
  '',
  ''
].join(NL);
s = s.replace(anchor, add + anchor);
fs.writeFileSync(p, s, 'utf8');
console.log('whohastest added');
