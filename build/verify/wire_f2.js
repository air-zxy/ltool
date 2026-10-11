const fs = require('fs');

function patch(p, pairs) {
  let s = fs.readFileSync(p, 'utf8');
  const crlf = s.includes('\r\n');
  if (crlf) s = s.split('\r\n').join('\n');
  for (const [a, b] of pairs) {
    const c = s.split(a).length - 1;
    if (c !== 1) throw new Error(p + ' anchor hit ' + c + ': ' + a.slice(0, 60));
    s = s.replace(a, b);
  }
  fs.writeFileSync(p, crlf ? s.split('\n').join('\r\n') : s);
  console.log('patched', p);
}

// 1) 音频还没实现，不要在 Options 里挂两个空开关
patch('src/Recorder.h', [[
  '        bool systemAudio = false;  // 录系统声音（WASAPI 环路）\n        bool micAudio = false;     // 录麦克风\n',
  ''
]]);

// 2) 选择器重建时那行 setText 是废的
patch('src/RecordUi.cpp', [[
  '        if (list->count())\n            list->setCurrentRow(0);\n        filter->setText(filter->text());   // 保持过滤词，重开时不用重打\n',
  '        if (list->count())\n            list->setCurrentRow(0);\n'
]]);

// 3) F2 热键：注册 + 分发
patch('src/main.cpp', [
  ['constexpr WPARAM kCaptureId = 2;',
   'constexpr WPARAM kCaptureId = 2;\nconstexpr WPARAM kRecordId = 3;'],
  ['&& (msg->wParam == kHotkeyId || msg->wParam == kCaptureId)) {',
   '&& (msg->wParam == kHotkeyId || msg->wParam == kCaptureId\n            || msg->wParam == kRecordId)) {'],
  ['        if (id == kCaptureId)\n            CaptureOverlay::start();\n        else\n            win.toggleVisible();',
   '        if (id == kCaptureId)\n            CaptureOverlay::start();\n        else if (id == kRecordId)\n            RecordUi::startViaPicker();\n        else\n            win.toggleVisible();'],
  ['    if (!RegisterHotKey(nullptr, (int)kCaptureId, MOD_NOREPEAT, VK_F1))\n        Utils::log(QString("F1 capture hotkey failed (err %1)").arg(GetLastError()));',
   '    if (!RegisterHotKey(nullptr, (int)kCaptureId, MOD_NOREPEAT, VK_F1))\n        Utils::log(QString("F1 capture hotkey failed (err %1)").arg(GetLastError()));\n    // F2 录屏（只录选中的那一个窗口）。失败只记日志：F2 被别的程序占了也不该拖住启动\n    if (!RegisterHotKey(nullptr, (int)kRecordId, MOD_NOREPEAT, VK_F2))\n        Utils::log(QString("F2 record hotkey failed (err %1)").arg(GetLastError()));'],
  ['#include "Recorder.h"', '#include "Recorder.h"\n#include "RecordUi.h"']
]);

// 4) CMake
patch('CMakeLists.txt', [['    src/Recorder.cpp', '    src/Recorder.cpp\n    src/RecordUi.cpp']]);
