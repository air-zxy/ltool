const fs = require('fs');
const NL = String.fromCharCode(13, 10);

function patch(path, edits) {
  let s = fs.readFileSync(path, 'utf8');
  for (const [from, to] of edits) {
    const i = s.indexOf(from);
    if (i < 0) throw new Error('missing in ' + path + ': ' + from.slice(0, 40));
    if (s.indexOf(from, i + from.length) >= 0 && from.length < 30)
      throw new Error('ambiguous: ' + from.slice(0, 40));
    s = s.slice(0, i) + to + s.slice(i + from.length);
  }
  fs.writeFileSync(path, s, 'utf8');
  console.log('patched ' + path);
}

// Utils.h：加 workingSetMB 声明
patch('D:/project/cpp/ltool/src/Utils.h', [[
  '// 让系统立刻回收工作集（任务管理器里的"内存"立即下降），用于重负载操作结束后' + NL +
  'void trimWorkingSet();',
  '// 让系统立刻回收工作集（任务管理器里的"内存"立即下降）。' + NL +
  '// 注意：这会让下一次交互把所有页换回来，别在常驻的热键路径上用它' + NL +
  'void trimWorkingSet();' + NL + NL +
  '// 当前进程工作集（MB），-1 = 读不到。诊断"唤出慢"用' + NL +
  'int workingSetMB();'
]]);

// Utils.cpp：实现 + include
patch('D:/project/cpp/ltool/src/Utils.cpp', [
  ['#include <tlhelp32.h>', '#include <tlhelp32.h>' + NL + '#include <psapi.h>'],
  ['void trimWorkingSet()' + NL + '{' + NL +
   '    const BOOL ok = SetProcessWorkingSetSize(GetCurrentProcess(), SIZE_T(-1), SIZE_T(-1));' + NL +
   '    log(QString("trim working set: %1").arg(ok ? "ok" : QString("failed err %1").arg(GetLastError())));' + NL +
   '}',
   'void trimWorkingSet()' + NL +
   '{' + NL +
   '    const BOOL ok = SetProcessWorkingSetSize(GetCurrentProcess(), SIZE_T(-1), SIZE_T(-1));' + NL +
   '    log(QString("trim working set: %1 (剩 %2MB)").arg(ok ? "ok" : QString("failed err %1").arg(GetLastError()))' + NL +
   '                   .arg(workingSetMB()));' + NL +
   '}' + NL + NL +
   'int workingSetMB()' + NL +
   '{' + NL +
   '    PROCESS_MEMORY_COUNTERS_EX pmc{};' + NL +
   '    pmc.cb = sizeof(pmc);' + NL +
   '    if (!K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))' + NL +
   '        return -1;' + NL +
   '    return int(pmc.WorkingSetSize / (1024 * 1024));' + NL +
   '}']
]);

// main.cpp：去掉定时 trim（每 10 分钟 + 启动 30 秒后各一次）
patch('D:/project/cpp/ltool/src/main.cpp', [[
  '    // 内存优化：索引完成后 + 每 10 分钟空闲时，让系统回收工作集' + NL +
  '    QTimer* trimTimer = new QTimer(&app);' + NL +
  '    trimTimer->setInterval(10 * 60 * 1000);' + NL +
  '    QObject::connect(trimTimer, &QTimer::timeout, &Utils::trimWorkingSet);' + NL +
  '    trimTimer->start();' + NL +
  '    QTimer::singleShot(30 * 1000, &app, &Utils::trimWorkingSet);',
  '    // 以前这里每 10 分钟 EmptyWorkingSet 一次，看起来"内存漂亮"，代价是下一次按热键' + NL +
  '    // 要把整个 Qt + 索引的页换回来，实测就是"唤出很卡"。只在截图那种一次性大块内存后回收。'
]]);
