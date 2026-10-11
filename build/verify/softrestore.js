const fs = require('fs');
const snap = fs.readFileSync('D:/project/cpp/ltool/build/verify/SoftwarePage.oct4.cpp', 'utf8');
let s = snap;

const forLine = '    for (int i = 0; i < kSoftCount; ++i) {\n';
const first = s.indexOf(forLine);
const rowStart = s.indexOf(forLine, first + forLine.length);
if (first < 0 || rowStart < 0) throw new Error('loops not found');
if (rowStart === first) throw new Error('only one loop?');

const bodyEnd = s.indexOf('\n    }\n', rowStart + forLine.length);
const body = s.slice(rowStart + forLine.length, bodyEnd);
if (body.indexOf('m_rowsLay->insertWidget') < 0) throw new Error('body is not the row loop');

const k = s.lastIndexOf('    constexpr int kStatusWidth = 172;', rowStart);
if (k < 0) throw new Error('kStatusWidth not found');
const fv = s.indexOf('    fillVersions();', rowStart);
const close = s.indexOf('\n}\n', fv);
const oldTail = s.slice(k, close + 3);

const NL = '\n';
const NEW = [
  '    const qint64 msMatch = all.elapsed();',
  '    m_fill = std::make_shared<SoftFill>();',
  '    m_fill->matches = matches;',
  '    m_fill->catOrder = catOrder;',
  '    m_fill->catTotal = catTotal;',
  '    m_fill->catInstalled = catInstalled;',
  '    // 建行控件才是这一页真正的大头（实测 27 行 224ms），以前全压在点"软件"那一下，',
  '    // 窗口要空白两百多毫秒才画出来。改成先让窗口出现，行在后续事件循环里分批建。',
  '    m_status->setText(QStringLiteral("正在准备 %1 个推荐软件…").arg(kSoftCount));',
  '    QTimer::singleShot(0, this, &SoftwarePage::buildRowsStep);',
  '    Utils::log(QStringLiteral("software rebuild: index=%1ms match=%2ms")',
  '                   .arg(msLoad).arg(msMatch - msLoad));',
  '}',
  '',
  'void SoftwarePage::buildRowsStep()',
  '{',
  '    if (!m_fill)',
  '        return;',
  '    QElapsedTimer budget;',
  '    budget.start();',
  '    constexpr int kStatusWidth = 172;',
  '    constexpr int kBtnsWidth = 238;   // 打开 + 版本 + 安装/更新 + 官网 的最宽情况',
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
].join(NL);

s = s.slice(0, k) + NEW + s.slice(close + 3);

// includes
s = s.replace('#include <QCryptographicHash>', '#include <QCryptographicHash>\n#include <QElapsedTimer>\n#include <QTimer>\n#include <memory>');

// SoftFill struct right before the anonymous namespace
const def = [
  '// 分批建行的中间状态：分类统计算一次就够，游标 i 记住建到第几个了',
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
].join(NL);
const ns = s.indexOf('\nnamespace {');
if (ns < 0) throw new Error('no anon namespace');
s = s.slice(0, ns + 1) + def + s.slice(ns + 1 + 'namespace {'.length);

fs.writeFileSync('D:/project/cpp/ltool/src/SoftwarePage.cpp', s, 'utf8');
console.log('written bytes=' + s.length + ' bodyLines=' + body.split(NL).length);
