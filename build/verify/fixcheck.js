const fs = require('fs');

function patch(path, reps) {
  let s = fs.readFileSync(path, 'utf8');
  const NL = s.includes('\r\n') ? '\r\n' : '\n';
  for (let [a, b] of reps) {
    a = a.split('\n').join(NL);
    b = b.split('\n').join(NL);
    if (!s.includes(a)) throw new Error('anchor missing in ' + path + ': ' + JSON.stringify(a.slice(0, 60)));
    s = s.replace(a, b);
  }
  fs.writeFileSync(path, s);
  console.log('patched', path);
}

patch('D:/project/cpp/ltool/src/InfoRow.cpp', [
  ['#include <QHBoxLayout>', '#include <QCheckBox>\n#include <QHBoxLayout>'],
]);

patch('D:/project/cpp/ltool/src/RegistryPage.cpp', [
  // 1) 不再依赖 item 的 checkState
  [`    connect(m_list, &QListWidget::itemChanged, this, [this] {
        int n = 0;
        for (int i = 0; i < m_list->count(); ++i)
            if (m_list->item(i)->checkState() == Qt::Checked)
                ++n;
        m_delBtn->setEnabled(n > 0 && !m_busy);
        if (!m_busy)
            m_status->setText(n > 0 ? QString("已选 %1 项").arg(n) : m_status->text());
    });
`, ''],

  // 2) 每次重建列表清空勾选
  [`void RegistryPage::rebuildList()
{
    m_list->clear();`,
   `void RegistryPage::rebuildList()
{
    m_list->clear();
    m_checked.clear();`],

  // 3) 行内勾选框取代 item flags
  [`            if (it.deletable()) {
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(Qt::Unchecked);
            } else {
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            }`,
   ''],

  [`            m_list->addItem(item);
            m_list->setItemWidget(item, row);`,
   `            if (it.deletable()) {
                row->makeCheckable(false);
                connect(row, &InfoRow::checkToggled, this, [this, i](bool on) {
                    if (on)
                        m_checked.insert(i);
                    else
                        m_checked.remove(i);
                    updateDeleteBtn();
                });
            }
            m_list->addItem(item);
            m_list->setItemWidget(item, row);`],

  // 4) 删除走 m_checked
  [`    QVector<int> picked;
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* it = m_list->item(i);
        if (it->checkState() == Qt::Checked)
            picked.append(it->data(Qt::UserRole).toInt());
    }`,
   `    QVector<int> picked;
    for (const int i : m_checked)
        picked.append(i);
    std::sort(picked.begin(), picked.end());`],
]);
