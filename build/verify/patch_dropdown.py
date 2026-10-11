import io

# ---------------- header ----------------
p = r'D:\project\cpp\ltool\src\LanguagePage.h'
h = io.open(p, encoding='utf-8').read()
h = h.replace('class QListWidget;', 'class QListWidget;\nclass QComboBox;')
h = h.replace('''    void switchVersion();
    void rebuildWingetVersions();   // winget 可安装版本（点击=静默安装）
    QString mainWingetId(const QString& langId) const;''',
'''    void rebuildVersions();         // 下拉框：本机已装（切换） + winget 可安装（静默装）
    void onVersionChosen(int index);
    void applyLocalVersion(const QString& path, const QString& label);
    QString mainWingetId(const QString& langId) const;''')
h = h.replace('    QPushButton* m_switchBtn = nullptr;\n', '')
h = h.replace('''    QListWidget* m_versionList = nullptr;
    QListWidget* m_wingetVerList = nullptr;
    QPushButton* m_wingetRefreshBtn = nullptr;
''', '''    QComboBox* m_versionBox = nullptr;
    QPushButton* m_versionRefreshBtn = nullptr;
''')
h = h.replace('    QStringList m_versions;                   // 检测到的版本命令输出（label 行）\n', '')
io.open(p, 'w', encoding='utf-8', newline='\n').write(h)

# ---------------- cpp ----------------
p = r'D:\project\cpp\ltool\src\LanguagePage.cpp'
s = io.open(p, encoding='utf-8').read()

# 1) ctor: two groups -> one dropdown row
old = '''    auto* versionGroup = new QGroupBox("版本（点击查看，切换写入环境变量）", right);
    auto* versionLay = new QVBoxLayout(versionGroup);
    m_versionList = new QListWidget(versionGroup);
    m_versionList->setMaximumHeight(120);
    m_switchBtn = new QPushButton("切换到此版本", versionGroup);
    versionLay->addWidget(m_versionList);
    versionLay->addWidget(m_switchBtn);

    auto* wingetGroup = new QGroupBox("可安装版本（winget · 点击即静默安装到配置的安装目录）", right);
    auto* wingetLay = new QVBoxLayout(wingetGroup);
    m_wingetVerList = new QListWidget(wingetGroup);
    m_wingetVerList->setMaximumHeight(130);
    m_wingetVerList->setToolTip("单击任意版本即后台静默安装；安装目录取配置页的「安装目录」");
    m_wingetRefreshBtn = new QPushButton("⟳ 重新查询版本列表", wingetGroup);
    wingetLay->addWidget(m_wingetVerList);
    wingetLay->addWidget(m_wingetRefreshBtn);
'''
assert old in s, 'ctor group'
new = '''    auto* versionGroup = new QGroupBox(
        "版本（下拉直接切换；选「可安装」的会静默安装到配置的安装目录）", right);
    auto* versionLay = new QHBoxLayout(versionGroup);
    versionLay->setSpacing(8);
    m_versionBox = new QComboBox(versionGroup);
    m_versionBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_versionBox->setToolTip("● 开头 = 当前生效版本；「已装」= 本机已有，选中即切换；"
                             "「可安装」= winget 上的版本，选中后静默安装");
    m_versionRefreshBtn = new QPushButton("⟳", versionGroup);
    m_versionRefreshBtn->setFixedWidth(36);
    m_versionRefreshBtn->setToolTip("重新检测本机版本，并联网刷新 winget 版本列表");
    versionLay->addWidget(m_versionBox, 1);
    versionLay->addWidget(m_versionRefreshBtn);
'''
s = s.replace(old, new, 1)

old = '''    rightLay->addWidget(versionGroup);
    rightLay->addWidget(wingetGroup);'''
assert old in s, 'addWidget'
s = s.replace(old, '''    rightLay->addWidget(versionGroup);''', 1)

# 2) connects
old = '''    connect(m_wingetRefreshBtn, &QPushButton::clicked, this, [this] {'''
assert old in s, 'connect anchor'
s = s.replace(old, '''    connect(m_versionBox, &QComboBox::activated, this, &LanguagePage::onVersionChosen);
    connect(m_versionRefreshBtn, &QPushButton::clicked, this, [this] {''', 1)

# 3) refreshDetail: drop the old local list block, use rebuildVersions()
old = '''    // 版本
    m_versionList->clear();
    const QString activePath = activeVersionPath(id);
    for (const DevEnv::LangVersion& v : DevEnv::detectVersions(id)) {
        const bool active = !activePath.isEmpty()
                            && QString::compare(QDir::toNativeSeparators(v.path), activePath,
                                                Qt::CaseInsensitive) == 0;
        m_versionList->addItem(active ? "● 当前  " + v.label : v.label);
        m_versionList->item(m_versionList->count() - 1)->setData(Qt::UserRole, v.path);
    }
    if (m_versionList->count() == 0)
        m_versionList->addItem("（未发现已安装版本，可用上方按钮安装）");
'''
assert old in s, 'refreshDetail versions'
s = s.replace(old, '', 1)

old = '''    m_currentWingetId = mainWingetId(id);
    rebuildWingetVersions();
'''
assert old in s, 'winget refresh'
s = s.replace(old, '''    m_currentWingetId = mainWingetId(id);
    rebuildVersions();
''', 1)

# 4) rebuildWingetVersions -> rebuildVersions (combo)
old_start = s.index('// 版本列表几乎不变：优先用 versions.json 缓存，只有首次或手动刷新才联网\nvoid LanguagePage::rebuildWingetVersions()')
old_end = s.index('void LanguagePage::installLanguage()')
new_rebuild = '''// 下拉框内容：本机已装（选中即切换） + winget 可安装版本（选中后确认再静默安装）
// 版本列表几乎不变：winget 部分优先用 versions.json 缓存，只有点 ⟳ 才联网
void LanguagePage::rebuildVersions()
{
    const QString id = m_currentLang;
    const bool wasBlocked = m_versionBox->blockSignals(true);
    m_versionBox->clear();

    const QString activePath = activeVersionPath(id);
    int localCount = 0;
    int activeIndex = -1;
    for (const DevEnv::LangVersion& v : DevEnv::detectVersions(id)) {
        const QString path = QDir::toNativeSeparators(v.path);
        const bool active = !activePath.isEmpty()
                            && QString::compare(path, activePath, Qt::CaseInsensitive) == 0;
        m_versionBox->addItem(QString("%1已装  %2").arg(active ? QStringLiteral("● ") : QString(),
                                                        v.label),
                              QVariantList{QStringLiteral("local"), path, v.label});
        if (active)
            activeIndex = m_versionBox->count() - 1;
        ++localCount;
    }
    if (localCount == 0)
        m_versionBox->addItem("（本机未发现已装版本）", QVariantList{QStringLiteral("none"),
                                                                    QString(), QString()});

    if (!m_currentWingetId.isEmpty()) {
        const QStringList cached = DevEnv::cachedVersions(m_currentWingetId);
        for (const QString& v : cached)
            m_versionBox->addItem(QString("可安装  %1").arg(v),
                                  QVariantList{QStringLiteral("remote"), v, v});
        if (cached.isEmpty())
            m_versionBox->addItem("（点 ⟳ 联网获取 winget 版本列表）",
                                  QVariantList{QStringLiteral("none"), QString(), QString()});
    }

    if (activeIndex >= 0)
        m_versionBox->setCurrentIndex(activeIndex);
    m_versionBox->blockSignals(wasBlocked);
}

void LanguagePage::onVersionChosen(int index)
{
    if (index < 0)
        return;
    const QVariantList d = m_versionBox->itemData(index).toList();
    const QString kind = d.value(0).toString();
    const QString val = d.value(1).toString();

    if (kind == QLatin1String("local")) {
        if (!val.isEmpty() && val == activeVersionPath(m_currentLang))
            return;
        applyLocalVersion(val, d.value(2).toString());
        return;
    }
    if (kind == QLatin1String("remote")) {
        const QString appName = langName(m_currentLang);
        if (QMessageBox::question(this, "ltool",
                QString("静默安装 %1 的 %2 到\\n%3\\n\\nwinget 在后台执行，进度见 log.txt。")
                    .arg(appName, val, DevEnv::installDirFor(appName)))
            != QMessageBox::Yes) {
            rebuildVersions();   // 取消：退回当前生效项
            return;
        }
        QString msg;
        if (!DevEnv::startSilentInstall(m_currentWingetId, val, appName, &msg)) {
            QMessageBox::warning(this, "ltool", msg);
            return;
        }
        m_status->setText(QString("正在后台静默安装 %1 %2 → %3（详情见 log.txt）")
                              .arg(appName, val, DevEnv::installDirFor(appName)));
        return;
    }
    rebuildVersions();
}

'''
s = s[:old_start] + new_rebuild + s[old_end:]

# 5) switchVersion -> applyLocalVersion(path,label)
old = '''void LanguagePage::switchVersion()
{
    const int row = m_versionList->currentRow();
    if (row < 0 || row >= m_versionList->count()) {
        QMessageBox::information(this, "ltool", "请先在版本列表中选择一个版本。");
        return;
    }
    QListWidgetItem* it = m_versionList->item(row);
    if (it->text().startsWith("●") || it->data(Qt::UserRole).toString().isEmpty())
        return;

    const QString path = it->data(Qt::UserRole).toString();
    QString err;'''
assert old in s, 'switchVersion head'
s = s.replace(old, '''void LanguagePage::applyLocalVersion(const QString& path, const QString& label)
{
    if (path.isEmpty()) {
        QMessageBox::information(this, "ltool", "这个条目没有可切换的本机路径。");
        return;
    }
    QString err;''', 1)

old = '''        ok = QProcess::startDetached("cmd.exe",
            {"/c", "start", "", "cmd", "/k", "rustup default " + it->text()});'''
assert old in s, 'rust branch'
s = s.replace(old, '''        ok = QProcess::startDetached("cmd.exe",
            {"/c", "start", "", "cmd", "/k", "rustup default " + label});''', 1)

io.open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('LanguagePage dropdown done')
