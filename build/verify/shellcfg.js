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

patch('D:/project/cpp/ltool/src/ConfigPage.h', [
  ['    QCheckBox* m_favorites = nullptr;',
   '    QCheckBox* m_favorites = nullptr;' + NL +
   '    QCheckBox* m_shellMenu = nullptr;' + NL +
   '    QLabel* m_shellInfo = nullptr;']
]);

patch('D:/project/cpp/ltool/src/ConfigPage.cpp', [
  // include
  ['#include "ConfigPage.h"', '#include "ConfigPage.h"' + NL + '#include "ShellMenu.h"'],
  // 新分组：插在"微软拼音"那组之前
  ['    auto* ime = new QGroupBox("微软拼音 · 中文输入时的标点", this);',
   '    auto* shell = new QGroupBox("资源管理器右键菜单", this);' + NL +
   '    auto* shellLay = new QFormLayout(shell);' + NL +
   '    m_shellMenu = new QCheckBox("在文件 / 文件夹的右键里加入 ltool 子菜单", shell);' + NL +
   '    m_shellMenu->setToolTip(QStringLiteral("子菜单里现在是：粉碎（不可恢复）、占用分析、谁占用着它。\\n"' + NL +
   '                                      "解压 / 压缩要等压缩包后端编进来才会出现。"));' + NL +
   '    shellLay->addRow(m_shellMenu);' + NL +
   '    m_shellInfo = new QLabel(shell);' + NL +
   '    m_shellInfo->setObjectName("muted");' + NL +
   '    m_shellInfo->setWordWrap(true);' + NL +
   '    shellLay->addRow("当前：", m_shellInfo);' + NL + NL +
   '    auto* ime = new QGroupBox("微软拼音 · 中文输入时的标点", this);'],
  // 布局里挂上去：紧跟在 general 之后
  ['    lay->addWidget(general);',
   '    lay->addWidget(general);' + NL + '    lay->addWidget(shell);'],
  // load
  ['    m_clipInfo->setText(QString("%1 条记录 · 图片存在 clipimg\\\\").arg(ClipboardStore::instance().items().size()));' + NL +
   '    showImeState(m_imePunct, m_imePunctInfo);',
   '    m_clipInfo->setText(QString("%1 条记录 · 图片存在 clipimg\\\\").arg(ClipboardStore::instance().items().size()));' + NL +
   '    m_shellMenu->setChecked(ShellMenu::installed());' + NL +
   '    m_shellInfo->setText(ShellMenu::installed()' + NL +
   '                              ? QStringLiteral("已集成。%1").arg(ShellMenu::locationHint())' + NL +
   '                              : QStringLiteral("未集成（默认）。勾上并保存才会写入；取消勾选再保存即撤销。"));' + NL +
   '    showImeState(m_imePunct, m_imePunctInfo);'],
  // save：这一项是"立刻生效"的系统集成，勾/不勾直接改注册表
  ['    QSettings cfg(Utils::dataDir() + "/config.ini", QSettings::IniFormat);' + NL +
   '    Utils::setAutostart(m_autostart->isChecked());',
   '    QSettings cfg(Utils::dataDir() + "/config.ini", QSettings::IniFormat);' + NL +
   '    Utils::setAutostart(m_autostart->isChecked());' + NL +
   '    if (m_shellMenu->isChecked() != ShellMenu::installed()) {' + NL +
   '        const QString r = m_shellMenu->isChecked() ? ShellMenu::install() : ShellMenu::remove();' + NL +
   '        m_shellInfo->setText(r);' + NL +
   '        m_shellMenu->setChecked(ShellMenu::installed());' + NL +
   '    }']
]);
