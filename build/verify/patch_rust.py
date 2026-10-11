import io

# ---------- DevEnv.h ----------
p = r'D:\project\cpp\ltool\src\DevEnv.h'
h = io.open(p, encoding='utf-8').read()
old = '    QString wingetId;     // 缺失时用 winget 安装的包 ID（可为空）'
assert old in h
h = h.replace(old, old + '\n'
    '    QString versionId;    // 列版本/装指定版本用的包 ID；空则同 wingetId\n'
    '                        // （rust 的 wingetId 是 Rustup，但 Rust 语言版本在 Rustlang.Rust.MSVC）', 1)
io.open(p, 'w', encoding='utf-8', newline='\n').write(h)

# ---------- DevEnv.cpp ----------
p = r'D:\project\cpp\ltool\src\DevEnv.cpp'
s = io.open(p, encoding='utf-8').read()
old = ('    { "label": "rustc", "cmd": ["rustc", "--version"], "wingetId": "Rustlang.Rustup" },\n'
       '    { "label": "cargo", "cmd": ["cargo", "--version"], "wingetId": "Rustlang.Rustup" }')
assert old in s, 'rust template'
s = s.replace(old,
    '    { "label": "rustc", "cmd": ["rustc", "--version"], "wingetId": "Rustlang.Rustup",\n'
    '      "versionId": "Rustlang.Rust.MSVC" },\n'
    '    { "label": "cargo", "cmd": ["cargo", "--version"], "wingetId": "Rustlang.Rustup",\n'
    '      "versionId": "Rustlang.Rust.MSVC" }', 1)

old = '            it.wingetId = io.value("wingetId").toString();'
assert old in s, 'parse'
s = s.replace(old, old + '\n'
    '            it.versionId = io.value("versionId").toString();\n'
    '            // 兼容已经写出的旧 rust.json：rustup 的版本号（1.29.x）不是 Rust 语言版本（1.97.x）\n'
    '            if (it.versionId.isEmpty() && it.wingetId == QLatin1String("Rustlang.Rustup"))\n'
    '                it.versionId = QStringLiteral("Rustlang.Rust.MSVC");', 1)
io.open(p, 'w', encoding='utf-8', newline='\n').write(s)

# ---------- LanguagePage.h ----------
p = r'D:\project\cpp\ltool\src\LanguagePage.h'
h = io.open(p, encoding='utf-8').read()
h = h.replace('class QListWidget;\nclass QComboBox;',
              'class QListWidget;\nclass QComboBox;\nclass QGroupBox;')
h = h.replace('    QString mainWingetId(const QString& langId) const;',
              '    QString mainWingetId(const QString& langId) const;\n'
              '    QString mainVersionId(const QString& langId) const;')
h = h.replace('    QString m_versionFetching;                // 正在联网抓版本列表的包 id',
              '    QString m_versionFetching;                // 正在联网抓版本列表的包 id\n'
              '    QString m_currentVersionId;               // 列版本/装版本用的包 id\n'
              '    QGroupBox* m_versionGroup = nullptr;')
io.open(p, 'w', encoding='utf-8', newline='\n').write(h)

# ---------- LanguagePage.cpp ----------
p = r'D:\project\cpp\ltool\src\LanguagePage.cpp'
s = io.open(p, encoding='utf-8').read()

old = ('    auto* versionGroup = new QGroupBox(\n'
       '        "版本（下拉直接切换；选「可安装」的会静默安装到配置的安装目录）", right);')
assert old in s, 'group'
s = s.replace(old, '    m_versionGroup = new QGroupBox("版本", right);\n'
                   '    auto* versionGroup = m_versionGroup;', 1)

old = '''QString LanguagePage::mainWingetId(const QString& langId) const
{'''
assert old in s, 'mainWingetId'
s = s.replace(old, '''// 列版本/装指定版本用的包：优先 versionId（rust 的 Rustup 版本号不是 Rust 语言版本）
QString LanguagePage::mainVersionId(const QString& langId) const
{
    for (const DevPlugin& p : DevEnv::loadPlugins()) {
        if (p.id != langId)
            continue;
        for (const DevItem& it : p.items) {
            if (!it.versionId.isEmpty())
                return it.versionId;
            if (!it.wingetId.isEmpty())
                return it.wingetId;
        }
    }
    return {};
}

''' + old, 1)

old = '''    m_currentWingetId = mainWingetId(id);
    rebuildVersions();'''
assert old in s, 'refreshDetail hook'
s = s.replace(old, '''    m_currentWingetId = mainWingetId(id);
    m_currentVersionId = mainVersionId(id);
    rebuildVersions();''', 1)

old = '''    const QString id = m_currentLang;
    const bool wasBlocked = m_versionBox->blockSignals(true);
    m_versionBox->clear();'''
assert old in s, 'rebuild head'
s = s.replace(old, '''    const QString id = m_currentLang;
    const bool wasBlocked = m_versionBox->blockSignals(true);
    m_versionBox->clear();
    if (m_versionGroup)
        m_versionGroup->setTitle(m_currentVersionId.isEmpty()
            ? QStringLiteral("版本（下拉直接切换本机已装版本）")
            : QStringLiteral("版本（下拉切换本机版本；「可安装」列表与安装都走 %1，装到配置的安装目录）")
                  .arg(m_currentVersionId));''', 1)

pairs = [
    ('    if (!m_currentWingetId.isEmpty()) {\n        const QStringList cached = DevEnv::cachedVersions(m_currentWingetId);',
     '    if (!m_currentVersionId.isEmpty()) {\n        const QStringList cached = DevEnv::cachedVersions(m_currentVersionId);'),
    ('            if (m_versionFetching.isEmpty())\n                m_versionFetching = m_currentWingetId;\n'
     '            m_versionBox->addItem(\n                m_versionFetching == m_currentWingetId',
     '            if (m_versionFetching.isEmpty())\n                m_versionFetching = m_currentVersionId;\n'
     '            m_versionBox->addItem(\n                m_versionFetching == m_currentVersionId'),
    ('            if (m_versionFetching == m_currentWingetId) {\n                const QString id = m_currentWingetId;',
     '            if (m_versionFetching == m_currentVersionId) {\n                const QString vid = m_currentVersionId;'),
    ('                DevEnv::fetchVersionsAsync(id, [this, id](QStringList versions) {\n'
     '                    if (m_versionFetching == id)\n'
     '                        m_versionFetching.clear();\n'
     '                    if (versions.isEmpty()) {\n'
     '                        m_status->setText(QString("%1 联网获取版本列表失败").arg(id));\n'
     '                        return;\n'
     '                    }\n'
     '                    if (m_currentWingetId == id)\n'
     '                        rebuildVersions();\n'
     '                });',
     '                DevEnv::fetchVersionsAsync(vid, [this, vid](QStringList versions) {\n'
     '                    if (m_versionFetching == vid)\n'
     '                        m_versionFetching.clear();\n'
     '                    if (versions.isEmpty()) {\n'
     '                        m_status->setText(QString("%1 联网获取版本列表失败").arg(vid));\n'
     '                        return;\n'
     '                    }\n'
     '                    if (m_currentVersionId == vid)\n'
     '                        rebuildVersions();\n'
     '                });'),
    ('        if (!DevEnv::startSilentInstall(m_currentWingetId, val, appName, &msg)) {',
     '        if (!DevEnv::startSilentInstall(m_currentVersionId, val, appName, &msg)) {'),
    ('        rebuildVersions();   // 本机版本重新检测\n        if (m_currentWingetId.isEmpty())\n            return;\n        const QString id = m_currentWingetId;',
     '        rebuildVersions();   // 本机版本重新检测\n        if (m_currentVersionId.isEmpty())\n            return;\n        const QString id = m_currentVersionId;'),
    ('            if (m_currentWingetId != id)\n                return;\n            m_status->setText(versions.isEmpty()',
     '            if (m_currentVersionId != id)\n                return;\n            m_status->setText(versions.isEmpty()'),
]
for a, b in pairs:
    assert a in s, 'MISS: ' + a[:60]
    s = s.replace(a, b, 1)
io.open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('rust versionId wired')
