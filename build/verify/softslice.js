const fs = require('fs');
const p = 'D:/project/cpp/ltool/src/SoftwarePage.cpp';
let s = fs.readFileSync(p, 'utf8');

const forLine = '    for (int i = 0; i < kSoftCount; ++i) {\n';
const LS = s.indexOf(forLine);
if (LS < 0) throw new Error('loop start not found');
const fv = s.indexOf('    fillVersions();', LS);
if (fv < 0) throw new Error('fillVersions not found');
const te = s.indexOf('\n}\n', fv);
if (te < 0) throw new Error('rebuild end not found');
const body = s.slice(LS + forLine.length, s.indexOf('\n    }\n', LS) );
const oldRegion = s.slice(LS, te + 3);

const NEW = [
  '    constexpr int kStatusWidth = 172;',
  '    constexpr int kBtnsWidth = 238;   // 打开 + 版本 + 安装/更新 + 官网 的最宽情况',
  '    m_fill = std::make_shared<SoftFill>();',
  '    m_fill->matches = matches;',
  '    m_fill->catOrder = catOrder;',
  '    m_fill->catTotal = catTotal;',
  '    m_fill->catInstalled = catInstalled;',
  '    Utils::log(QStringLiteral("software rebuild: index=%1ms match=%2ms rows=sliced later")',
  '                   .arg(msLoad).arg(msMatch - msLoad));',
  '    // 建行控件是这一页真正的大头（实测 27 行 224ms），以前全压在点"软件"那一下，',
  '    // 窗口要空白 200 多毫秒才出来。现在先让窗口画出来，行在后续事件循环里分批建。',
  '    m_status->setText(QStringLiteral("正在准备 %1 个推荐软件…").arg(kSoftCount));',
  '    QTimer::singleShot(0, this, &SoftwarePage::buildRowsStep);',
  '}',
  '',
  'void SoftwarePage::buildRowsStep()',
  '{',
  '    if (!m_fill)',
  '        return;',
  '    QElapsedTimer budget;',
  '    budget.start();',
  '    constexpr int kStatusWidth = 172;',
  '    constexpr int kBtnsWidth = 238;',
  '    QVector<AppEntry>& matches = m_fill->matches;',
  '    QVector<QString>& catOrder = m_fill->catOrder;',
  '    QVector<int>& catTotal = m_fill->catTotal;',
  '    QVector<int>& catInstalled = m_fill->catInstalled;',
  '    QHash<QString, QWidget*>& headOf = m_fill->headOf;',
  '    while (m_fill->i < kSoftCount && (m_fill->i == 0 || budget.elapsed() < 8)) {',
  '        const int i = m_fill->i;',
  '        ++m_fill->i;',
  '        ++m_fill->built;',
  body,
  '    }',
  '    if (m_fill->i < kSoftCount) {',
  '        QTimer::singleShot(0, this, &SoftwarePage::buildRowsStep);',
  '        return;',
  '    }',
  '    const int rows = m_fill->built;',
  '    m_fill.reset();',
  '    applyFilter();',
  '    fillVersions();',
  '    Utils::log(QStringLiteral("software rows: %1 built").arg(rows));',
  '}',
  ''
].join('\n');

s = s.slice(0, LS) + NEW + s.slice(te + 3);

// SoftFill 定义 + include
const def = [
  '// 分批建行的中间状态：分类统计这些算一次就够，游标 i 记住建到第几个',
  'struct SoftFill {',
  '    QVector<AppEntry> matches;',
  '    QVector<QString> catOrder;',
  '    QVector<int> catTotal;',
  '    QVector<int> catInstalled;',
  '    QHash<QString, QWidget*> headOf;',
  '    int i = 0;',
  '    int built = 0;',
  '};',
  '',
  'namespace {'
].join('\n');
const ns = s.indexOf('namespace {');
if (ns < 0) throw new Error('anon namespace not found');
s = s.slice(0, ns) + def + s.slice(ns + 'namespace {'.length);

if (s.indexOf('#include <QTimer>') < 0)
  s = s.replace('#include <QDir>', '#include <QDir>\r\n#include <QTimer>'.replace(/\r\n/g, '\n'));
if (s.indexOf('#include <memory>') < 0)
  s = s.replace('#include <QDir>', '#include <memory>\n#include <QDir>');

fs.writeFileSync(p, s, 'utf8');
console.log('ok; body lines=' + body.split('\n').length);
