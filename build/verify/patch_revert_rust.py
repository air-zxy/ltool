import io

# ---- 1) DevEnv.h: 去掉 versionId ----
p = r'D:\project\cpp\ltool\src\DevEnv.h'
h = io.open(p, encoding='utf-8').read()
old = ('\n    QString versionId;    // 列版本/装指定版本用的包 ID；空则同 wingetId\n'
       '                        // （rust 的 wingetId 是 Rustup，但 Rust 语言版本在 Rustlang.Rust.MSVC）')
assert old in h
h = h.replace(old, '', 1)
io.open(p, 'w', encoding='utf-8', newline='\n').write(h)

# ---- 2) DevEnv.cpp: 模板与兼容映射都回到 Rustup ----
p = r'D:\project\cpp\ltool\src\DevEnv.cpp'
s = io.open(p, encoding='utf-8').read()
old = ('    { "label": "rustc", "cmd": ["rustc", "--version"], "wingetId": "Rustlang.Rustup",\n'
       '      "versionId": "Rustlang.Rust.MSVC" },\n'
       '    { "label": "cargo", "cmd": ["cargo", "--version"], "wingetId": "Rustlang.Rustup",\n'
       '      "versionId": "Rustlang.Rust.MSVC" }')
assert old in s, 'rust template'
s = s.replace(old,
    '    { "label": "rustc", "cmd": ["rustc", "--version"], "wingetId": "Rustlang.Rustup" },\n'
    '    { "label": "cargo", "cmd": ["cargo", "--version"], "wingetId": "Rustlang.Rustup" }', 1)

old = ('\n            it.versionId = io.value("versionId").toString();\n'
       '            // 兼容已经写出的旧 rust.json：rustup 的版本号（1.29.x）不是 Rust 语言版本（1.97.x）\n'
       '            if (it.versionId.isEmpty() && it.wingetId == QLatin1String("Rustlang.Rustup"))\n'
       '                it.versionId = QStringLiteral("Rustlang.Rust.MSVC");')
assert old in s, 'parse shim'
s = s.replace(old, '', 1)
io.open(p, 'w', encoding='utf-8', newline='\n').write(s)

# ---- 3) LanguagePage: 去掉 mainVersionId/m_currentVersionId，标题里标出包名 ----
p = r'D:\project\cpp\ltool\src\LanguagePage.h'
h = io.open(p, encoding='utf-8').read()
h = h.replace('\n    QString mainVersionId(const QString& langId) const;', '', 1)
h = h.replace('\n    QString m_currentVersionId;               // 列版本/装版本用的包 id', '', 1)
io.open(p, 'w', encoding='utf-8', newline='\n').write(h)

p = r'D:\project\cpp\ltool\src\LanguagePage.cpp'
s = io.open(p, encoding='utf-8').read()

old = s[s.index('// 列版本/装指定版本用的包：优先 versionId'):s.index('QString LanguagePage::mainWingetId')]
s = s.replace(old, '', 1)

s = s.replace('''    m_currentWingetId = mainWingetId(id);
    m_currentVersionId = mainVersionId(id);
    rebuildVersions();''', '''    m_currentWingetId = mainWingetId(id);
    rebuildVersions();''', 1)

# 标题：明确"可安装"列表来自哪个 winget 包（rustup 的版本号本来就与 rustc 不同）
s = s.replace('''    if (m_versionGroup)
        m_versionGroup->setTitle(m_currentVersionId.isEmpty()
            ? QStringLiteral("版本（下拉直接切换本机已装版本）")
            : QStringLiteral("版本（下拉切换本机版本；「可安装」列表与安装都走 %1，装到配置的安装目录）")
                  .arg(m_currentVersionId));''',
'''    if (m_versionGroup)
        m_versionGroup->setTitle(m_currentWingetId.isEmpty()
            ? QStringLiteral("版本（下拉直接切换本机已装版本）")
            : QStringLiteral("版本（下拉切换本机版本；「可安装」= %1 的版本，装到配置的安装目录）")
                  .arg(m_currentWingetId));''', 1)

for a, b in [
    ('    if (!m_currentVersionId.isEmpty()) {\n        const QStringList cached = DevEnv::cachedVersions(m_currentVersionId);',
     '    if (!m_currentWingetId.isEmpty()) {\n        const QStringList cached = DevEnv::cachedVersions(m_currentWingetId);'),
    ('            if (m_versionFetching.isEmpty())\n                m_versionFetching = m_currentVersionId;\n'
     '            m_versionBox->addItem(\n                m_versionFetching == m_currentVersionId',
     '            if (m_versionFetching.isEmpty())\n                m_versionFetching = m_currentWingetId;\n'
     '            m_versionBox->addItem(\n                m_versionFetching == m_currentWingetId'),
    ('            if (m_versionFetching == m_currentVersionId) {\n                const QString vid = m_currentVersionId;',
     '            if (m_versionFetching == m_currentWingetId) {\n                const QString vid = m_currentWingetId;'),
    ('                    if (m_currentVersionId == vid)\n                        rebuildVersions();',
     '                    if (m_currentWingetId == vid)\n                        rebuildVersions();'),
    ('        if (!DevEnv::startSilentInstall(m_currentVersionId, val, appName, &msg)) {',
     '        if (!DevEnv::startSilentInstall(m_currentWingetId, val, appName, &msg)) {'),
    ('        rebuildVersions();   // 本机版本重新检测\n        if (m_currentVersionId.isEmpty())\n            return;\n        const QString id = m_currentVersionId;',
     '        rebuildVersions();   // 本机版本重新检测\n        if (m_currentWingetId.isEmpty())\n            return;\n        const QString id = m_currentWingetId;'),
    ('            if (m_currentVersionId != id)\n                return;\n            m_status->setText(versions.isEmpty()',
     '            if (m_currentWingetId != id)\n                return;\n            m_status->setText(versions.isEmpty()'),
]:
    assert a in s, 'MISS ' + a[:60]
    s = s.replace(a, b, 1)
io.open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('reverted to Rustup + package name shown in title')
