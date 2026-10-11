const fs = require('fs');

function patch(p, pairs) {
  let s = fs.readFileSync(p, 'utf8');
  const crlf = s.includes('\r\n');
  if (crlf) s = s.split('\r\n').join('\n');
  for (const kv of pairs) {
    const a = kv[0], b = kv[1];
    const c = s.split(a).length - 1;
    if (c !== 1) throw new Error(p + ' anchor hit ' + c + ': ' + a.slice(0, 60));
    s = s.replace(a, b);
  }
  fs.writeFileSync(p, crlf ? s.split('\n').join('\r\n') : s);
  console.log('patched', p);
}

// 控件要给上 objectName：不然 findChild<QLabel*> 抓到的是那个红点，测试读不到文字
patch('src/RecordUi.cpp', [
  ['    setStyleSheet("QWidget{background:#26272c;border:1px solid #3a3c45;}"',
   '    list->setObjectName(QStringLiteral("list"));\n    filter->setObjectName(QStringLiteral("filter"));\n    setStyleSheet("QWidget{background:#26272c;border:1px solid #3a3c45;}"'],
  ['    m_text = new QLabel(QStringLiteral("正在录…"), this);',
   '    m_text = new QLabel(QStringLiteral("正在录…"), this);\n    m_text->setObjectName(QStringLiteral("text"));'],
  ['    m_stop = new QPushButton(QStringLiteral("停止"), this);',
   '    m_stop = new QPushButton(QStringLiteral("停止"), this);\n    m_stop->setObjectName(QStringLiteral("stop"));']
]);

patch('src/main.cpp', [
  ['#include <QLabel>\n#include <QThread>', '#include <QLabel>\n#include <QListWidget>\n#include <QThread>'],
  ['auto* plist = picker.findChild<QListWidget*>();',
   'auto* plist = picker.findChild<QListWidget*>(QStringLiteral("list"));'],
  ['auto* text = bar.findChild<QLabel*>();',
   'auto* text = bar.findChild<QLabel*>(QStringLiteral("text"));'],
  ['auto* stop = bar.findChild<QPushButton*>();',
   'auto* stop = bar.findChild<QPushButton*>(QStringLiteral("stop"));']
]);
