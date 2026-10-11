import io

# ================= SoftwarePage =================
p = r'D:\project\cpp\ltool\src\SoftwarePage.h'
h = io.open(p, encoding='utf-8').read()
h = h.replace(
    '    void download(const QString& name, const QString& wingetId, const QString& url);',
    '    void download(const QString& name, const QString& wingetId, const QString& url);\n'
    '    void installVersion(const QString& wingetId, const QString& version);')
h = h.replace(
    '    QPushButton* m_refreshBtn = nullptr;',
    '    QPushButton* m_refreshBtn = nullptr;\n'
    '    QLabel* m_status = nullptr;')
io.open(p, 'w', encoding='utf-8', newline='\n').write(h)

p = r'D:\project\cpp\ltool\src\SoftwarePage.cpp'
s = io.open(p, encoding='utf-8').read()
old = '''    lay->addWidget(scroll, 1);

    connect(m_refreshBtn, &QPushButton::clicked, this, &SoftwarePage::rebuild);'''
assert old in s
s = s.replace(old, '''    lay->addWidget(scroll, 1);

    m_status = new QLabel(this);
    m_status->setObjectName("muted");
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    lay->addWidget(m_status);

    connect(m_refreshBtn, &QPushButton::clicked, this, &SoftwarePage::rebuild);''', 1)

old = '''    DevItem tmp;
    tmp.label = name;
    tmp.wingetId = wingetId;
    QString err;
    DevEnv::runInstall(this, tmp, &err);
}'''
assert old in s
s = s.replace(old, '''    installVersion(wingetId, QString());   // 静默装到配置的安装目录
}''', 1)
io.open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('SoftwarePage done')

# ================= LanguagePage =================
p = r'D:\project\cpp\ltool\src\LanguagePage.h'
h = io.open(p, encoding='utf-8').read()
h = h.replace(
    '    void switchVersion();',
    '    void switchVersion();\n'
    '    void rebuildWingetVersions();   // winget 可安装版本（点击=静默安装）\n'
    '    QString mainWingetId(const QString& langId) const;')
h = h.replace(
    '    QListWidget* m_relatedList = nullptr;',
    '    QListWidget* m_wingetVerList = nullptr;\n'
    '    QPushButton* m_wingetRefreshBtn = nullptr;\n'
    '    QListWidget* m_relatedList = nullptr;')
h = h.replace(
    '    QString m_currentLang;                    // 当前选中语言 id',
    '    QString m_currentLang;                    // 当前选中语言 id\n'
    '    QString m_currentWingetId;                // 该语言的主 winget 包')
io.open(p, 'w', encoding='utf-8', newline='\n').write(h)

p = r'D:\project\cpp\ltool\src\LanguagePage.cpp'
s = io.open(p, encoding='utf-8').read()

# 1) new group box in the ctor
old = '''    auto* relatedGroup = new QGroupBox("关联软件（双击启动本地应用 / 打开官网）", right);'''
assert old in s
s = s.replace(old, '''    auto* wingetGroup = new QGroupBox("可安装版本（winget · 点击即静默安装到配置的安装目录）", right);
    auto* wingetLay = new QVBoxLayout(wingetGroup);
    m_wingetVerList = new QListWidget(wingetGroup);
    m_wingetVerList->setMaximumHeight(130);
    m_wingetVerList->setToolTip("单击任意版本即后台静默安装；安装目录取配置页的「安装目录」");
    m_wingetRefreshBtn = new QPushButton("⟳ 重新查询版本列表", wingetGroup);
    wingetLay->addWidget(m_wingetVerList);
    wingetLay->addWidget(m_wingetRefreshBtn);

    auto* relatedGroup = new QGroupBox("关联软件（双击启动本地应用 / 打开官网）", right);''', 1)

old = '''    rightLay->addWidget(versionGroup);
    rightLay->addWidget(relatedGroup);'''
assert old in s
s = s.replace(old, '''    rightLay->addWidget(versionGroup);
    rightLay->addWidget(wingetGroup);
    rightLay->addWidget(relatedGroup);''', 1)

# 2) wiring
old = '''    connect(m_redetectBtn, &QPushButton::clicked, this, &LanguagePage::refreshDetail);'''
assert old in s
s = s.replace(old, '''    connect(m_redetectBtn, &QPushButton::clicked, this, &LanguagePage::refreshDetail);
    connect(m_wingetRefreshBtn, &QPushButton::clicked, this, [this] {
        if (m_currentWingetId.isEmpty())
            return;
        const QString id = m_currentWingetId;
        m_status->setText("正在联网获取版本列表…");
        DevEnv::fetchVersionsAsync(id, [this, id](QStringList versions) {
            if (m_currentWingetId != id)
                return;
            m_status->setText(QString("%1 版本列表已更新（%2 个版本）").arg(id).arg(versions.size()));
            rebuildWingetVersions();
        });
    });
    connect(m_wingetVerList, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        const QString v = it->data(Qt::UserRole).toString();
        if (v.isEmpty())
            return;
        QString msg;
        if (!DevEnv::startSilentInstall(m_currentWingetId, v, &msg)) {
            QMessageBox::warning(this, "ltool", msg);
            return;
        }
        m_status->setText(QString("正在后台静默安装 %1 v%2 → %3（详情见 log.txt）")
                              .arg(m_currentWingetId, v, DevEnv::installLocation()));
    });''', 1)

# 3) refreshDetail: remember the main winget id + rebuild the list
old = '''    detectAsync(id);
}'''
assert old in s
s = s.replace(old, '''    m_currentWingetId = mainWingetId(id);
    rebuildWingetVersions();

    detectAsync(id);
}

QString LanguagePage::mainWingetId(const QString& langId) const
{
    for (const DevPlugin& p : DevEnv::loadPlugins()) {
        if (p.id != langId)
            continue;
        for (const DevItem& it : p.items)
            if (!it.wingetId.isEmpty())
                return it.wingetId;
    }
    return {};
}

// 版本列表几乎不变：优先用 versions.json 缓存，只有首次或手动刷新才联网
void LanguagePage::rebuildWingetVersions()
{
    m_wingetVerList->clear();
    if (m_currentWingetId.isEmpty()) {
        auto* it = new QListWidgetItem("该语言没有配置 winget 包（可在 plugins/<id>.json 里填 wingetId）",
                                       m_wingetVerList);
        it->setFlags(Qt::NoItemFlags);
        return;
    }
    const QStringList cached = DevEnv::cachedVersions(m_currentWingetId);
    const qint64 fetched = DevEnv::versionCacheTime(m_currentWingetId);
    if (cached.isEmpty()) {
        auto* it = new QListWidgetItem("（还没有版本缓存，点右下「⟳ 重新查询版本列表」获取）",
                                       m_wingetVerList);
        it->setFlags(Qt::NoItemFlags);
        const QString id = m_currentWingetId;
        DevEnv::fetchVersionsAsync(id, [this, id](QStringList versions) {
            if (m_currentWingetId != id)
                return;
            m_status->setText(QString("%1 版本列表已缓存（%2 个版本）").arg(id).arg(versions.size()));
            rebuildWingetVersions();
        });
        return;
    }
    auto* head = new QListWidgetItem(
        QString("%1   缓存于 %2   ·   点击安装")
            .arg(m_currentWingetId,
                 fetched > 0 ? QDateTime::fromMSecsSinceEpoch(fetched).toString("yyyy-MM-dd")
                             : "未知时间"),
        m_wingetVerList);
    head->setFlags(Qt::NoItemFlags);
    for (const QString& v : cached) {
        auto* it = new QListWidgetItem(QString("    安装 %1").arg(v), m_wingetVerList);
        it->setData(Qt::UserRole, v);
        it->setToolTip(QString("winget --silent --version %1 --location %2")
                           .arg(v, DevEnv::installLocation()));
    }
}''', 1)

# 4) installLanguage -> silent
old = '''            if (!it.wingetId.isEmpty()) {
                QString err;
                DevEnv::runInstall(this, it, &err);
                refreshDetail();
                return;
            }'''
assert old in s
s = s.replace(old, '''            if (!it.wingetId.isEmpty()) {
                QString msg;
                if (!DevEnv::startSilentInstall(it.wingetId, QString(), &msg)) {
                    QMessageBox::warning(this, "ltool", msg);
                    return;
                }
                m_status->setText(QString("正在后台静默安装 %1 → %2（详情见 log.txt）")
                                      .arg(it.wingetId, DevEnv::installLocation()));
                refreshDetail();
                return;
            }''', 1)
io.open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('LanguagePage done')
