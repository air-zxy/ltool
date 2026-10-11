const fs = require('fs');
const NL = String.fromCharCode(13, 10);

function patch(path, edits) {
  let s = fs.readFileSync(path, 'utf8');
  for (const [from, to] of edits) {
    const i = s.indexOf(from);
    if (i < 0) throw new Error('missing in ' + path + ': ' + from.slice(0, 60));
    if (s.indexOf(from, i + from.length) >= 0) throw new Error('AMBIGUOUS in ' + path + ': ' + from.slice(0, 60));
    s = s.slice(0, i) + to + s.slice(i + from.length);
  }
  fs.writeFileSync(path, s, 'utf8');
  console.log('patched ' + path);
}

// main.cpp：include + 右键动作分发（必须排在单实例守卫之前）
patch('D:/project/cpp/ltool/src/main.cpp', [
  ['#include "SilentInstallLog.h"',
   '#include "ShellAction.h"' + NL + '#include "SilentInstallLog.h"'],
  ['    // 单实例：第二个实例唤醒已运行的窗口后退出',
   '    // 资源管理器右键菜单进来的动作：这是另起的一个进程，得先于单实例守卫处理，' + NL +
   '    // 否则它只会把主窗口叫醒然后自己退出。' + NL +
   '    for (const char* f : { "--shred", "--fileinfo", "--whohas" }) {' + NL +
   '        if (args.contains(QLatin1String(f))) {' + NL +
   '            return ShellAction::run(QString::fromLatin1(f), args.mid(args.indexOf(QLatin1String(f)) + 1).value(0));' + NL +
   '        }' + NL +
   '    }' + NL + NL +
   '    // 单实例：第二个实例唤醒已运行的窗口后退出']
]);

// CMakeLists：新文件
patch('D:/project/cpp/ltool/CMakeLists.txt', [
  ['    src/UninstallPage.cpp',
   '    src/UninstallPage.cpp' + NL + '    src/ShellMenu.cpp' + NL + '    src/ShellAction.cpp' + NL + '    src/FileOps.cpp']
]);
