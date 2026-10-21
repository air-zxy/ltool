#include "Utils.h"

#include "Theme.h"
#include "WebShortcuts.h"   // 默认 webs.json 的那份字节只有它算得出（"什么算默认值"不许有第二份答案）

#include "platform/Paths.h"
#include "platform/ProcInfo.h"

#include <QClipboard>
#include <QCryptographicHash>
#include <functional>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileIconProvider>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPainterPath>
#include <QApplication>
#include <QElapsedTimer>
#include <QLocalSocket>
#include <QSet>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QThread>

#include <QSettings>


namespace Utils {

namespace {

// `%APPDATA%`（没有就用 Qt 那个等价位置）。老的数据目录就是 `<这里>\ltool`。
QString legacyBase()
{
    QString base = qEnvironmentVariable("APPDATA");
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base;
}

// exe 旁边写不写得进去：和 mediaRoot() 同一套问法（写一个临时文件再删掉）。
// 探的时候不预先建子目录 —— 否则"问一句目录在哪"就会在构建目录里留下一个空文件夹，跟着安装包出去。
bool dirIsWritable(const QString& dir)
{
    const QFileInfo fi(dir);
    if (dir.isEmpty() || !fi.isDir() || !fi.isWritable())
        return false;
    const QString probe = dir + "/.ltool-write-test";
    QFile f(probe);
    if (!f.open(QIODevice::WriteOnly) || f.write("x") != 1)
        return false;
    f.close();
    QFile::remove(probe);
    return true;
}

} // namespace

QString resolveDataDir(const QString& appDir, const QString& envOverride, const QString& appData,
                       bool appDirWritable)
{
    // ①环境变量：自测沙箱和"便携模式"都走这一档，它最大
    if (!envOverride.isEmpty())
        return QDir::fromNativeSeparators(envOverride);
    // ②默认：软件目录（exe 旁边）下的 data\ —— 他 2026-10-21 点名"所有的日志和数据都放软件目录下，
    //   不要放在 C 盘"。以前这里只有 %APPDATA%\ltool 一档，378 MB（深索引 320 + OCR 模型 33 +
    //   剪贴板图 16）全压在系统盘上。
    if (appDirWritable)
        return QDir::fromNativeSeparators(appDir) + "/data";
    // ③装到只读位置（比如塞进 Program Files）：退回老地方，并留一条日志（下面 dataDir() 里）
    return appData + "/ltool";
}

QString dataDir()
{
    static const QString cached = [] {
        const QString app = QDir::fromNativeSeparators(QCoreApplication::applicationDirPath());
        const QString env = qEnvironmentVariable("LTOOL_DATA_DIR");
        const QString appData = legacyBase();
        const bool writable = dirIsWritable(app);
        const QString d = resolveDataDir(app, env, appData, writable);
        if (env.isEmpty() && !writable)
            log(QStringLiteral("数据目录：%1 写不了，退回 %2（这就是以前那个 %APPDATA%\\ltool 的位置）")
                    .arg(app.isEmpty() ? QStringLiteral("(拿不到 exe 目录)") : app, d));
        QDir().mkpath(d);
        return d;
    }();
    return cached;
}

namespace {

// 老目录 / 现在这个目录，再加上"这趟到底有没有搬家可谈"。
// 三处共用同一套判断（搬家、"别往还没搬的目录里写程序自带的默认值"、补搬），判断不许有第二份 ——
// 2026-10-21 那次丢数据丢的正是"这几处对同一件事各有说法"。
bool legacyPair(QString* legacy, QString* now, QString* refusal)
{
    if (!qEnvironmentVariable("LTOOL_DATA_DIR").isEmpty()) {
        if (refusal)
            *refusal = QStringLiteral("没搬：数据目录被 LTOOL_DATA_DIR 指走了（沙箱/便携模式）");
        return false;
    }
    const QString l = QDir::fromNativeSeparators(legacyBase()) + "/ltool";
    const QString n = dataDir();
    if (QDir::cleanPath(n).compare(QDir::cleanPath(l), Qt::CaseInsensitive) == 0) {
        if (refusal)
            *refusal = QStringLiteral("没搬：数据目录还在老位置 %1（说明这一版没落到软件目录下）").arg(n);
        return false;
    }
    if (!QFileInfo::exists(l))
        return false;              // 新装的机器，本来就没有（这条不值得写一行日志）
    *legacy = l;
    *now = n;
    return true;
}

// 这个目录已经接住过你的真数据了吗：搬过留下的标记，或已经有一份真配置。
bool alreadySettled(const QString& now)
{
    return QFileInfo::exists(now + "/.migrated-from-appdata") || QFileInfo::exists(now + "/config.ini");
}

QByteArray sha256Of(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    QCryptographicHash h(QCryptographicHash::Sha256);
    return h.addData(&f) ? h.result().toHex() : QByteArray();   // 设备那一版内部就是分块读，320MB 不整块进内存
}

// 补搬那一步：复制 + 对字节，对不上就删掉半成品（老目录那份一个字都不动）。
bool copyOneVerified(const QString& from, const QString& to)
{
    QDir().mkpath(QFileInfo(to).absolutePath());
    if (!QFile::copy(from, to))
        return false;
    const QByteArray a = sha256Of(from), b = sha256Of(to);
    if (a.isEmpty() || a != b) {
        QFile::remove(to);
        return false;
    }
    return true;
}

} // namespace

int copyVerifiedTree(const QString& from, const QString& to, QStringList* problems, int* skipped)
{
    int copied = 0;
    std::function<void(const QString&, const QString&)> walk = [&](const QString& src, const QString& dst) {
        QDir dir(src);
        for (const QFileInfo& e : dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)) {
            const QString dstPath = dst + '/' + e.fileName();
            if (e.isDir()) {
                QDir().mkpath(dstPath);
                walk(e.absoluteFilePath(), dstPath);
                continue;
            }
            if (QFileInfo::exists(dstPath)) {
                // 目标里已经有同名文件 = 这一趟自己先写出来的（最典型就是 `log.txt`：
                // dataDir() 一被问到就会 mkpath 并写日志）。**不覆盖**，跳过并记一笔。
                if (skipped)
                    ++(*skipped);
                continue;
            }
            if (!QFile::copy(e.absoluteFilePath(), dstPath)) {
                if (problems)
                    *problems << QStringLiteral("%1 没复制过去").arg(e.fileName());
                continue;
            }
            // 对内容，不对"名字大小时间" —— 名字和大小都对也可能是没复制完的半成品
            const QByteArray a = sha256Of(e.absoluteFilePath()), b = sha256Of(dstPath);
            if (a.isEmpty() || a != b) {
                QFile::remove(dstPath);
                if (problems)
                    *problems << QStringLiteral("%1 复制完对不上哈希，那份已删").arg(e.fileName());
                continue;
            }
            ++copied;
        }
    };
    QDir().mkpath(to);
    walk(from, to);
    return copied;
}

// 一次性搬家：老 `%APPDATA%\ltool` 还在、新目录是空的 → 逐个复制，每个都按 sha256 对过才算搬成。
// **旧目录原样留着不删** —— 那是 378 MB 真实数据（配置、使用频率、深索引、剪贴板历史），
// 删错了没法复；对完账之后由人自己决定什么时候清，note 里把旧路径原样写出来。
int clearStockDefaultsForMigration(const QString& dir)
{
    // 「什么算内置默认」只有一份答案：拿写出默认值的那颗函数再算一遍字节。
    const QVector<QPair<QString, QByteArray>> stock = {
        { QStringLiteral("webs.json"), WebShortcuts::stockWebsJson() },
    };
    int removed = 0;
    for (const auto& s : stock) {
        if (s.second.isEmpty())
            continue;                       // 拿不到默认表就什么都不删（宁可少删不可多删）
        QFile t(dir + "/" + s.first);
        if (!t.open(QIODevice::ReadOnly))
            continue;
        const QByteArray have = t.readAll();
        t.close();
        if (have == s.second && t.remove())
            ++removed;
    }
    return removed;
}

int migrateLegacyDataDir(QString* note)
{
    // 沙箱 / 便携模式（`LTOOL_DATA_DIR` 设着）永远不搬：那 378 MB 是真实数据，
    // 而自测沙箱每次都是空的 —— 少这一道守卫，任何一条旗标判据都会把它整份复制进 %TEMP%。
    // 这条和下面两条都收在 `legacyPair()` 里（同一个判断不许有第二份）。
    QString legacy, now, refusal;
    if (!legacyPair(&legacy, &now, &refusal)) {
        if (note && !refusal.isEmpty())
            *note = refusal;
        return 0;
    }
    return migrateBetween(legacy, now, note);
}

bool migrationPendingFor(const QString& legacy, const QString& now)
{
    if (!QFileInfo::exists(legacy))
        return false;                                       // 这台机器本来就没有老目录
    if (QDir::cleanPath(now).compare(QDir::cleanPath(legacy), Qt::CaseInsensitive) == 0)
        return false;                                       // 还住在老位置：没什么可搬的
    return !alreadySettled(now);
}

bool legacyMigrationPending()
{
    QString legacy, now;
    // `legacyPair` 里那两条（设着 LTOOL_DATA_DIR 的沙箱/便携模式、老位置没变）先问掉，
    // 剩下的那两个目录再交给同一套判断的纯函数那一版 —— 判断只有一份。
    return legacyPair(&legacy, &now, nullptr) && migrationPendingFor(legacy, now);
}

int migrateBetween(const QString& legacy, const QString& now, QString* note)
{
    // 守卫不能是"目录非空"：`dataDir()` 一被问到就 mkpath，而 `Utils::log()` 立刻在里面写下
    // `log.txt` —— 本机第一次实测就是这么没搬成的（deliver 的 installed 那一栏先跑了
    // `--plugintest`，新目录里已经有一条日志，relaunch 那次就判定"搬过了"）。
    // 只认两种真信号：搬过留下的那颗标记，或新目录里已经有一份真配置。
    // 守卫不能是"目录非空"：`dataDir()` 一被问到就 mkpath，而 `Utils::log()` 立刻在里面写下
    // `log.txt` —— 本机第一次实测就是这么没搬成的（deliver 的 installed 那一栏先跑了
    // `--plugintest`，新目录里已经有一条日志，relaunch 那次就判定"搬过了"）。
    // 只认两种真信号：搬过留下的那颗标记，或新目录里已经有一份真配置。
    // 到这颗函数里老目录一定存在、也一定和 now 不是同一个位置，所以"还没搬"就等价于"没这两种信号"。
    if (!migrationPendingFor(legacy, now)) {
        // 已经搬过了 ≠ 搬干净了：那一趟可能被"目标里已有同名文件"挡住过真数据
        //（2026-10-21 本机就是这么把 24 条网站关键字留在老目录里的，而标记照写、第二次启动不再搬）。
        // 所以这一趟按两条明确的规矩补一次，其余一概不动。
        QStringList actions;
        const int fixed = backfillFromLegacy(legacy, now, &actions);
        if (note)
            *note = fixed > 0
                ? QStringLiteral("已经搬过了，这一趟又补回 %1 个文件（历史里引用着、本地却缺的剪贴板图）：%2")
                      .arg(fixed).arg(actions.join(QStringLiteral("；")))
                : QStringLiteral("没搬：%1 里已经有配置或搬过的标记（也没什么可补的）").arg(now);
        return fixed;
    }

    QStringList problems;
    int skipped = 0;
    // 先把目标里那些"程序自己刚写的默认表"清掉，否则下面那条"同名不覆盖"会让默认值挡住真数据
    //（2026-10-21 就是这么把 24 条网站关键字留在老目录里、界面上只剩默认 2 条的）。
    const int gaveWay = clearStockDefaultsForMigration(now);
    const int copied = copyVerifiedTree(legacy, now, &problems, &skipped);
    if (copied > 0) {
        QFile mark(now + "/.migrated-from-appdata");
        if (mark.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            mark.write(QStringLiteral("从 %1 搬来 %2 个文件（跳过同名 %3 个）").arg(legacy).arg(copied).arg(skipped)
                           .toUtf8());
            mark.close();
        }
    }
    if (note)
        *note = QStringLiteral("数据目录搬到软件目录下了：%1 个文件 · 旧目录 %2 "
                               "原样留着没删，确认新位置没问题之后自己删")
                    .arg(copied).arg(legacy);
    if (gaveWay > 0 && note)
        *note += QStringLiteral(" · %1 份程序自带的默认表给用户数据让了路").arg(gaveWay);
    if (!problems.isEmpty() && note)
        *note += QStringLiteral(" · %1 个没搬成：%2").arg(problems.size()).arg(problems.join(QStringLiteral("；")));
    if (skipped > 0 && note)
        *note += QStringLiteral(" · 另有 %1 个同名文件没覆盖（比如这一趟自己的 log.txt）").arg(skipped);
    return copied;
}

int backfillFromLegacy(const QString& legacy, const QString& now, QStringList* actions)
{
    int done = 0;
    // 只补这一件事：**现在这份 clipboard.json 正引用着、而本地缺失的图**，从老目录补回来。
    //
    // 为什么不做"目标里那份是默认表就拿老目录那份换掉"（搬家**之前**那一步做的是它的另一半）：
    // ① 1.4.0 还没打 tag、没发出去，所以没有任何用户装过带这个 bug 的版本 = 没有需要救的现场
    //    （要救的现场在搬家**之前**那条路上已经被 `clearStockDefaultsForMigration` +
    //     "别往还没搬的目录里写默认值"那两道堵住了）；
    // ② 这条判据拿"逐字节等于**这一版**自带的默认"当"里面没有你的东西"，而默认表的文字本身是
    //    会跟着版本改的（本机 `plugins\java.json` 两份就这么差着一条 note 的措辞）——
    //    拿老目录那份换掉，可能是把新版的默认文字退回上一版。宁可不做，也不静默做一件说不清的事。
    //
    // 也不做"整棵 clipimg 缺什么补什么"：那样你后来自己删掉的往期图会被每次启动复活
    //（"删不掉"比"少一张"更让人难受），所以只认历史里真的引用着的那些。
    QFile cj(now + "/clipboard.json");
    if (cj.open(QIODevice::ReadOnly)) {
        const QJsonArray arr = QJsonDocument::fromJson(cj.readAll()).array();
        QSet<QString> stems;
        for (const QJsonValue& v : arr) {
            const QJsonObject o = v.toObject();
            if (!o.value("image").toBool())
                continue;
            const QString p = o.value("path").toString();
            if (p.isEmpty())
                continue;
            // 按**文件名**（那颗本来就是图内容的 sha1）问，不按记着的那串绝对路径问：
            // 记的可能是搬家前的老位置，那条路径现在还打得开，可你一删旧目录它就断了 ——
            // 所以"本地这个 clipimg 里有没有"才是这里要的那个条件。
            stems.insert(QFileInfo(p).baseName());
        }
        if (!stems.isEmpty()) {
            const QDir oldImg(legacy + "/clipimg");
            const auto entries = oldImg.entryInfoList(QDir::Files);
            for (const QFileInfo& fi : entries) {
                if (!stems.contains(fi.baseName()))
                    continue;
                const QString to = now + "/clipimg/" + fi.fileName();
                if (QFileInfo::exists(to))
                    continue;
                if (copyOneVerified(fi.absoluteFilePath(), to)) {
                    ++done;
                    if (actions)
                        *actions << QStringLiteral("clipimg/%1").arg(fi.fileName());
                }
            }
        }
    }
    return done;
}

QString iconsDir()   { return dataDir() + "/icons"; }
QString pluginsDir() { return dataDir() + "/plugins"; }
QString backupsDir() { return dataDir() + "/backups"; }

// 截图（含往期）和录像的存放位置：配置页可改，键是 paths/captures / paths/recordings，
// 留空 = 软件目录下的 captures / recordings（见 mediaRoot()）。
// 这里故意不缓存 —— 改了要立刻生效。
namespace {
QString configuredDir(const char* key, const QString& fallback)
{
    QSettings cfg(dataDir() + "/config.ini", QSettings::IniFormat);
    const QString v = cfg.value(QLatin1String(key)).toString().trimmed();
    return v.isEmpty() ? fallback : QDir::fromNativeSeparators(v);
}
} // namespace

// 截图/录像默认落在**软件目录**（exe 旁边）下面，不往 C 盘的 %APPDATA% 堆。
// 一个进程只探一次；探的时候只往 exe 旁边写一个临时文件，不预先建 captures 子目录
// —— 否则"问一句目录在哪"就会在构建目录里留下一个空文件夹，跟着安装包一起出去。
// 装到只读位置 / 没写权限的机器（比如塞进 Program Files）退回数据目录，并留一条日志。
QString mediaRoot()
{
    static const QString root = [] {
        const QString app = QDir::fromNativeSeparators(QCoreApplication::applicationDirPath());
        const QFileInfo fi(app);
        if (fi.isDir() && fi.isWritable()) {
            const QString probe = app + "/.ltool-write-test";
            QFile f(probe);
            if (f.open(QIODevice::WriteOnly) && f.write("x") == 1) {
                f.close();
                QFile::remove(probe);
                return app;
            }
        }
        log(QStringLiteral("媒体目录：%1 写不了，截图/录像退回 %2").arg(app, dataDir()));
        return dataDir();
    }();
    return root;
}

QString capturesDir()   { return configuredDir("paths/captures", mediaRoot() + "/captures"); }
QString recordingsDir() { return configuredDir("paths/recordings", mediaRoot() + "/recordings"); }

void ensureDirs()
{
    QDir().mkpath(dataDir());
    QDir().mkpath(iconsDir());
    QDir().mkpath(pluginsDir());
    QDir().mkpath(backupsDir());
}

QIcon cachedFileIcon(const QString& path)
{
    static QMutex mu;
    static QHash<QString, QIcon> cache;

    QMutexLocker lock(&mu);
    auto it = cache.constFind(path);
    if (it != cache.constEnd())
        return it.value();

    QIcon icon;
    QFileInfo fi(path);
    QFileIconProvider prov;
    if (fi.isDir())
        icon = prov.icon(QFileIconProvider::Folder);
    else
        icon = prov.icon(fi);
    if (icon.isNull())
        icon = prov.icon(QFileIconProvider::File);

    cache.insert(path, icon);
    return icon;
}

QString exeFromCommand(const QString& cmd)
{
    QString s = cmd.trimmed();
    if (s.isEmpty())
        return {};
    if (s.startsWith(QLatin1Char('"'))) {
        const int e = s.indexOf(QLatin1Char('"'), 1);
        return e > 0 ? s.mid(1, e - 1) : s.mid(1);
    }
    for (int i = 1; i < s.size(); ++i) {
        if (s[i] == QLatin1Char(' ') && i + 1 < s.size()
            && (s[i + 1] == QLatin1Char('/') || s[i + 1] == QLatin1Char('-')))
            return s.left(i);                       // 参数段一定以 " -" / " /" 开头
    }
    static const char* kExeExt[] = { ".exe", ".dll", ".sys", ".com", ".msi" };
    for (const char* ext : kExeExt)
        if (s.endsWith(QLatin1String(ext), Qt::CaseInsensitive))
            return s;   // 整条就是文件名：哪怕文件已经没了，也不能按空格切
    if (QFileInfo::exists(Platform::expandEnv(s)))
        return s;
    for (int i = s.indexOf(QLatin1Char(' ')); i > 0; i = s.indexOf(QLatin1Char(' '), i + 1)) {
        const QString head = s.left(i);
        if (QFileInfo::exists(Platform::expandEnv(head)))
            return head;
    }
    const int sp = s.indexOf(QLatin1Char(' '));
    return sp > 0 ? s.left(sp) : s;
}

bool fileTargetMissing(const QString& raw)
{
    QString p = Platform::expandEnv(raw.trimmed());
    p.remove(QLatin1Char('"'));
    if (p.isEmpty())
        return false;                       // 没写过值 ≠ 失效
    if (QFileInfo::exists(p))
        return false;
    if (!p.contains(QLatin1Char('\\')) && !p.contains(QLatin1Char('/')))
        return Platform::findExecutable(p).isEmpty();   // system32 里那些不能算失效
    return true;
}

QIcon appIcon()
{
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(2, 2, 60, 60, 15, 15);
    p.fillPath(path, QColor(63, 106, 224));
    QFont f("Microsoft YaHei UI", 30);
    f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(pm.rect().adjusted(0, -2, 0, -2), Qt::AlignCenter, "L");
    return QIcon(pm);
}

void trayNote(const QString& title, const QString& msg, int msec)
{
    {
        QLocalSocket probe;
        probe.connectToServer(QString::fromWCharArray(kInstanceKey));
        if (probe.waitForConnected(200)) {
            probe.write("note\t" + title.toUtf8() + '\t' + msg.toUtf8() + "\n");
            probe.waitForBytesWritten(200);
            probe.disconnectFromServer();
            return;
        }
    }
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    QSystemTrayIcon tray;
    if (!QApplication::windowIcon().isNull())
        tray.setIcon(QApplication::windowIcon());
    tray.show();
    tray.showMessage(title, msg, QSystemTrayIcon::Information, msec);
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < msec) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(20);
    }
    tray.hide();
}

void log(const QString& msg)
{
    QFile f(dataDir() + "/log.txt");
    if (!f.open(QIODevice::Append | QIODevice::Text))
        return;
    f.write(QString("[%1] %2\n")
                .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"), msg)
                .toUtf8());
}

QString ConsoleLines::decode(const QByteArray& line)
{
    const QString u8 = QString::fromUtf8(line);
    return u8.contains(QChar(0xFFFD)) ? QString::fromLocal8Bit(line) : u8;
}

QString ConsoleLines::feed(const QByteArray& chunk)
{
    pending += chunk;
    QString out;
    int nl;
    while ((nl = pending.indexOf('\n')) >= 0) {
        const QByteArray line = pending.left(nl).trimmed();
        pending.remove(0, nl + 1);
        if (!line.isEmpty())
            out += decode(line) + QLatin1Char('\n');
    }
    return out;
}

QString ConsoleLines::tail()
{
    const QByteArray rest = pending.trimmed();
    pending.clear();
    return rest.isEmpty() ? QString() : decode(rest) + QLatin1Char('\n');
}

// 两颗菜单图标共用的那块底：48x48、透明底、圆角色块。
QPixmap tilePixmap(const QColor& color)
{
    QPixmap pm(48, 48);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(2, 2, 44, 44, 12, 12);
    p.fillPath(path, color);
    return pm;
}

QIcon letterIcon(const QString& text, const QColor& color)
{
    // 高频绘图缓存：搜索窗口每敲一个字及卡片网格渲染时会触发数十次图标生成，
    // 按 (色值, 首字符) 64位哈希秒级缓存，彻底消除重复的位图与文字光栅化开销。
    const ushort ch = text.isEmpty() ? 0 : text.at(0).unicode();
    const quint64 key = (quint64(color.rgba()) << 32) | quint64(ch);
    static QHash<quint64, QIcon> cache;
    auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return *it;

    QPixmap pm = tilePixmap(color);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f("Microsoft YaHei UI", 18);
    f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(pm.rect(), Qt::AlignCenter, text.left(1));
    const QIcon icon(pm);
    cache.insert(key, icon);
    return icon;
}

QIcon bagIcon(const QColor& color)
{
    QPixmap pm = tilePixmap(color);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(Qt::white, 2.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    // 袋身：上口一条横线，两边往里收到底 —— 梯形比矩形更像"拎着的东西"
    QPainterPath body;
    body.moveTo(13, 21);
    body.lineTo(35, 21);
    body.lineTo(33, 37);
    body.lineTo(15, 37);
    body.closeSubpath();
    p.drawPath(body);
    // 提手：半圆，两端正好落在袋口那条线上（椭圆中心 y=21，所以矩形 y = 21 - h/2）
    p.drawArc(QRectF(18.5, 13, 11, 16), 0, 180 * 16);
    return QIcon(pm);
}

bool renameStaged(const QString& stagedPath, const QString& destPath, QString* error)
{
    for (int i = 0; i < 25; ++i) {
        if (QFile::rename(stagedPath, destPath))
            return true;
        QThread::msleep(200);
    }
    if (error)
        *error = QStringLiteral("%1 已经写好了，但改名时被别的程序挡住（多半是杀毒正在扫它）。\n"
                                "请稍等几秒，把它手动改成 %2")
                     .arg(QFileInfo(stagedPath).fileName(), QFileInfo(destPath).fileName());
    return false;
}

int compareVersions(const QString& a, const QString& b)
{
    // 版本号某段开头的数字（"10-beta" 取到 10；开头不是数字按 0 算，不抛出去）
    const auto segNum = [](const QString& part) {
        int i = 0;
        while (i < part.size() && part.at(i).isDigit())
            ++i;
        return i == 0 ? 0 : part.left(i).toInt();
    };
    const int dashA = a.indexOf(QLatin1Char('-'));
    const int dashB = b.indexOf(QLatin1Char('-'));
    const QStringList pa = (dashA < 0 ? a : a.left(dashA)).split(QLatin1Char('.'), Qt::SkipEmptyParts);
    const QStringList pb = (dashB < 0 ? b : b.left(dashB)).split(QLatin1Char('.'), Qt::SkipEmptyParts);
    const int n = qMax(pa.size(), pb.size());
    for (int i = 0; i < n; ++i) {
        const int va = i < pa.size() ? segNum(pa[i]) : 0;
        const int vb = i < pb.size() ? segNum(pb[i]) : 0;
        if (va != vb)
            return va < vb ? -1 : 1;
    }
    if (dashA < 0 && dashB < 0)
        return 0;
    if (dashA < 0)
        return 1;                    // 同数字：稳定版赢
    if (dashB < 0)
        return -1;
    return QString::compare(a.mid(dashA + 1), b.mid(dashB + 1));   // 都带后缀：beta.2 > beta.1
}


// 弹层那两份：名字留着（已发布的插件 dll 把符号名写死进导入表了，见 #91），
// 里面转调 Theme —— 2026-10-20 加了夜间/日间切换之后，样式表只有一份的事实不能裂成两份。
QString darkMenuStyle()
{
    return Theme::menuStyle();
}

QString darkTipStyle()
{
    return Theme::tipStyle();
}

} // namespace Utils
