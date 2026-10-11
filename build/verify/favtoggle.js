const fs = require('fs');
const NL = String.fromCharCode(13, 10);

function once(s, anchor, file) {
  const i = s.indexOf(anchor);
  if (i < 0) throw new Error('missing anchor in ' + file + ': ' + anchor.slice(0, 50));
  if (s.indexOf(anchor, i + anchor.length) >= 0)
    throw new Error('AMBIGUOUS anchor in ' + file + ': ' + anchor.slice(0, 50));
  return i;
}

function patch(file, edits) {
  let s = fs.readFileSync(file, 'utf8');
  for (const [anchor, replacement] of edits) {
    const i = once(s, anchor, file);
    s = s.slice(0, i) + replacement + s.slice(i + anchor.length);
  }
  fs.writeFileSync(file, s, 'utf8');
  console.log('patched ' + file);
}

patch('D:/project/cpp/ltool/src/ConfigPage.h', [[
  '    QCheckBox* m_imePunct = nullptr;',
  '    QCheckBox* m_imePunct = nullptr;' + NL + '    QCheckBox* m_favorites = nullptr;'
]]);

patch('D:/project/cpp/ltool/src/ConfigPage.cpp', [
  ['    generalLay->addRow("默认搜索引擎：", m_engine);',
   '    generalLay->addRow("默认搜索引擎：", m_engine);' + NL +
   '    m_favorites = new QCheckBox("呼出时显示常用列表（应用 / 网站 / 工具 / 文件）", general);' + NL +
   '    m_favorites->setToolTip("打开：按下热键就展开常用列表，和以前一样。\\n"' + NL +
   '                            "关闭（默认）：呼出后只有一个搜索框，输入才出结果。");' + NL +
   '    generalLay->addRow(m_favorites);'],
  ['    m_engine->setCurrentIndex(idx < 0 ? 0 : idx);',
   '    m_engine->setCurrentIndex(idx < 0 ? 0 : idx);' + NL +
   '    m_favorites->setChecked(cfg.value("ui/favoritesOnEmpty", false).toBool());'],
  ['    cfg.setValue("web/engine", m_engine->currentData().toString());',
   '    cfg.setValue("web/engine", m_engine->currentData().toString());' + NL +
   '    cfg.setValue("ui/favoritesOnEmpty", m_favorites->isChecked());']
]);

patch('D:/project/cpp/ltool/src/SearchWindow.h', [[
  '    bool m_warmed = false;',
  '    bool m_warmed = false;' + NL + '    bool m_showFavorites = false;    // 配置里勾了才在空查询时展开常用列表'
]]);
