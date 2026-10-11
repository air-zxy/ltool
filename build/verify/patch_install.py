import io

# ---------- DevEnv.h / .cpp: rename the out-param to msg ----------
p = r'D:\project\cpp\ltool\src\DevEnv.h'
h = io.open(p, encoding='utf-8').read()
h = h.replace(
    '// 后台静默发起安装（无控制台窗口）；结果写 log.txt。返回 false = 参数不合法/无 winget\n'
    'bool startSilentInstall(const QString& wingetId, const QString& version = QString(),\n'
    '                        QString* err = nullptr);',
    '// 后台静默发起安装（无控制台窗口），结果写 log.txt。\n'
    '// msg 回填：失败时是原因，成功时是实际执行的命令。返回 false 表示没发起。\n'
    'bool startSilentInstall(const QString& wingetId, const QString& version = QString(),\n'
    '                        QString* msg = nullptr);')
io.open(p, 'w', encoding='utf-8', newline='\n').write(h)

p = r'D:\project\cpp\ltool\src\DevEnv.cpp'
s = io.open(p, encoding='utf-8').read()
old = '''bool startSilentInstall(const QString& wingetId, const QString& version, QString* err)
{
    if (wingetId.isEmpty()) {
        if (err)
            *err = "该条目没有配置 winget 包";
        return false;
    }
    if (!wingetAvailable()) {
        if (err)
            *err = "系统里没有 winget 命令";
        return false;
    }'''
new = '''bool startSilentInstall(const QString& wingetId, const QString& version, QString* msg)
{
    if (wingetId.isEmpty()) {
        if (msg)
            *msg = "该条目没有配置 winget 包";
        return false;
    }
    if (!wingetAvailable()) {
        if (msg)
            *msg = "系统里没有 winget 命令（安装 App Installer 后重试）";
        return false;
    }'''
assert old in s
s = s.replace(old, new, 1)
old2 = '''    p->start("cmd.exe", {"/c", cmd});
    if (err)
        *err = cmd;
    return true;
}'''
new2 = '''    p->start("cmd.exe", {"/c", cmd});
    if (msg)
        *msg = cmd;
    return true;
}'''
assert old2 in s
s = s.replace(old2, new2, 1)
io.open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('DevEnv api tidied')

# ---------- SoftwarePage: drop the local cache copies, use DevEnv ----------
p = r'D:\project\cpp\ltool\src\SoftwarePage.cpp'
s = io.open(p, encoding='utf-8').read()
start = s.index('// 版本列表缓存（持久化 versions.json')
end = s.index('void SoftwarePage::showVersions(')
s = s[:start] + s[end:]

old_head = '''void SoftwarePage::showVersions(const QString& keywords, const QString& url,
                                const QString& wingetId)
{
    const QStringList kws = keywords.split(';', Qt::SkipEmptyParts);
    const auto apps = AppScanner::loadIndex();

    QMenu menu(this);'''
assert old_head in s
# locate the whole winget section and replace it
sect_start = s.index(old_head)
sect_end = s.index('    // 本机已装版本：条目只写')
new_section = '''void SoftwarePage::installVersion(const QString& wingetId, const QString& version)
{
    QString msg;
    if (!DevEnv::startSilentInstall(wingetId, version, &msg)) {
        QMessageBox::warning(this, "ltool", msg);
        return;
    }
    m_status->setText(QString("正在后台静默安装 %1 %2 → %3（详情见 log.txt）")
                          .arg(wingetId, version.isEmpty() ? "最新版" : version,
                               DevEnv::installLocation()));
}

void SoftwarePage::showVersions(const QString& keywords, const QString& url,
                                const QString& wingetId)
{
    const QStringList kws = keywords.split(';', Qt::SkipEmptyParts);
    const auto apps = AppScanner::loadIndex();

    QMenu menu(this);
    menu.setToolTipsVisible(true);
    // 版本列表几乎不变：有缓存直接用，只有首次或点"重新查询"才联网
    if (!wingetId.isEmpty()) {
        const QStringList cached = DevEnv::cachedVersions(wingetId);
        const qint64 fetched = DevEnv::versionCacheTime(wingetId);
        if (!cached.isEmpty()) {
            menu.addSection(QString("全部版本（winget · 缓存于 %1）· 点击静默安装")
                                .arg(QDateTime::fromMSecsSinceEpoch(fetched)
                                         .toString("yyyy-MM-dd")));
            for (const QString& v : cached)
                menu.addAction(QString("安装 %1").arg(v), this,
                               [this, wingetId, v] { installVersion(wingetId, v); });
        }
        menu.addAction("⟳ 重新查询版本列表", this, [this, wingetId] {
            DevEnv::fetchVersionsAsync(wingetId, [this, wingetId](QStringList versions) {
                m_status->setText(QString("%1 版本列表已更新（%2 个版本）")
                                      .arg(wingetId).arg(versions.size()));
            });
        });
        if (cached.isEmpty()) {
            auto* busy = menu.addAction("正在联网获取版本列表…");
            busy->setEnabled(false);
            DevEnv::fetchVersionsAsync(wingetId, [this, wingetId](QStringList versions) {
                m_status->setText(QString("%1 版本列表已缓存（%2 个版本），再点“版本”即可选装")
                                      .arg(wingetId).arg(versions.size()));
            });
        }
    }

'''
s = s[:sect_start] + new_section + s[sect_end:]
io.open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('SoftwarePage rewired to DevEnv')
