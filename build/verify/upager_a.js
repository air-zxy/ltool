const fs = require('fs');
const p = 'D:/project/cpp/ltool/src/UninstallPage.cpp';
let s = fs.readFileSync(p, 'utf8');
const NL = '\n';

function cut(startMark, endMark, replacement, opts) {
  const a = s.indexOf(startMark);
  if (a < 0) throw new Error('start missing: ' + startMark.slice(0, 40));
  if (s.indexOf(startMark, a + startMark.length) >= 0)
    throw new Error('start AMBIGUOUS: ' + startMark.slice(0, 40));
  let b;
  if (endMark === null) b = s.length;
  else {
    b = s.indexOf(endMark, a);
    if (b < 0) throw new Error('end missing: ' + endMark.slice(0, 40));
  }
  s = s.slice(0, a) + replacement + s.slice(b);
  console.log('cut ok: ' + startMark.slice(0, 30) + ' -> ' + (b - a) + ' chars replaced');
}

// ---------- A: 构造函数里的版面（去掉右侧详情卡，改成整宽列表 + 底部分页条） ----------
const A = [
  '    m_list = new QListWidget(this);',
  '    m_list->setUniformItemSizes(true);',
  '    m_list->setIconSize(QSize(20, 20));',
  '    // 动作全在行上，不再需要"选中一行"这件事',
  '    m_list->setSelectionMode(QAbstractItemView::NoSelection);',
  '    m_list->setFocusPolicy(Qt::NoFocus);',
  '    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);',
  '    lay->addWidget(m_list, 1);',
  '',
  '    // 底栏：左边一句实话，右边分页',
  '    auto* bar = new QHBoxLayout;',
  '    bar->setSpacing(8);',
  '    m_status = new QLabel(this);',
  '    m_status->setObjectName(QStringLiteral("muted"));',
  '    m_status->setWordWrap(true);',
  '    m_prev = new QPushButton(QStringLiteral("← 上一页"), this);',
  '    m_pageInfo = new QLabel(this);',
  '    m_pageInfo->setObjectName(QStringLiteral("muted"));',
  '    m_next = new QPushButton(QStringLiteral("下一页 →"), this);',
  '    for (QPushButton* b : { m_prev, m_next }) {',
  '        b->setObjectName(QStringLiteral("rowBtn"));',
  '        b->setCursor(Qt::PointingHandCursor);',
  '        b->setFocusPolicy(Qt::NoFocus);',
  '    }',
  '    bar->addWidget(m_status, 1);',
  '    bar->addWidget(m_prev);',
  '    bar->addWidget(m_pageInfo);',
  '    bar->addWidget(m_next);',
  '    lay->addLayout(bar);',
  '',
  '    m_debounce = new QTimer(this);',
  '    m_debounce->setSingleShot(true);',
  '    m_debounce->setInterval(180);',
  '    connect(m_debounce, &QTimer::timeout, this, &UninstallPage::applyFilter);',
  '    // 搜索框：连打时只在停手后重排一次（每页只建 20 行，但仍然不值得每键都建）',
  '    connect(m_search, &QLineEdit::textChanged, this, [this] { m_debounce->start(); });',
  '    connect(rescan, &QPushButton::clicked, this, &UninstallPage::reload);',
  '    connect(sysToggle, &QPushButton::toggled, this, [this, sysToggle] {',
  '        m_showSystem = sysToggle->isChecked();',
  '        sysToggle->setText(m_showSystem ? QStringLiteral("隐藏系统组件")',
  '                                        : QStringLiteral("显示系统组件"));',
  '        reload();',
  '    });',
  '    connect(m_prev, &QPushButton::clicked, this, [this] { showPage(m_page - 1); });',
  '    connect(m_next, &QPushButton::clicked, this, [this] { showPage(m_page + 1); });',
  '}',
  '',
  ''
].join(NL);
cut('    auto* body = new QHBoxLayout;', NL + 'void UninstallPage::showEvent', A + NL + 'void UninstallPage::showEvent');

fs.writeFileSync(p, s, 'utf8');
console.log('stage A written');
