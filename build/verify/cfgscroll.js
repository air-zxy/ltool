const fs = require('fs');
const NL = String.fromCharCode(13, 10);
const p = 'D:/project/cpp/ltool/src/ConfigPage.cpp';
let s = fs.readFileSync(p, 'utf8');

function once(str, anchor, label) {
  const i = str.indexOf(anchor);
  if (i < 0) throw new Error('missing ' + label);
  if (str.indexOf(anchor, i + anchor.length) >= 0) throw new Error('AMBIGUOUS ' + label);
  return i;
}

// 1) include
let i = once(s, '#include <QGroupBox>', 'include anchor');
s = s.slice(0, i + '#include <QGroupBox>'.length) + NL + '#include <QScrollArea>' + s.slice(i + '#include <QGroupBox>'.length);

// 2) 构造：内容放进一个内层 widget，外面套 QScrollArea
i = once(s, '    auto* lay = new QVBoxLayout(this);', 'ctor layout');
s = s.replace('    auto* lay = new QVBoxLayout(this);' + NL +
              '    lay->setContentsMargins(12, 4, 4, 4);',
  '    // 配置项越加越多，早就超出窗口高度了：整页放进滚动区，不然下面的分组看不见也点不到' + NL +
  '    auto* outer = new QVBoxLayout(this);' + NL +
  '    outer->setContentsMargins(0, 0, 0, 0);' + NL +
  '    auto* scroller = new QScrollArea(this);' + NL +
  '    scroller->setObjectName(QStringLiteral("cfgScroll"));' + NL +
  '    scroller->setWidgetResizable(true);' + NL +
  '    scroller->setFrameShape(QFrame::NoFrame);' + NL +
  '    scroller->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);' + NL +
  '    auto* body = new QWidget;' + NL +
  '    body->setObjectName(QStringLiteral("cfgBody"));' + NL +
  '    scroller->setWidget(body);' + NL +
  '    outer->addWidget(scroller);' + NL +
  '    auto* lay = new QVBoxLayout(body);' + NL +
  '    lay->setContentsMargins(12, 4, 4, 4);');

// 3) 保存按钮留在滚动区外面
s = s.replace('    lay->addLayout(btnLay);' + NL + '    lay->addStretch(1);',
              '    outer->addLayout(btnLay);');

fs.writeFileSync(p, s, 'utf8');
console.log('ConfigPage 已套上滚动区');
