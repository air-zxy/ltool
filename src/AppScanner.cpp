#include "AppScanner.h"

#include "Utils.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>

#include <windows.h>
#include <objbase.h>
#include <shobjidl.h>

namespace {

// 一个 .lnk 解析出来的东西
struct LinkInfo {
    QString target;     // 目标 exe/文档的绝对路径（可能为空）
    QString args;       // 启动参数
    QString iconPath;   // 图标位置（.lnk 里可以单独指定）
    int iconIndex = 0;
};

LinkInfo resolveLink(const QString& lnkPath)
{
    LinkInfo info;
    IShellLinkW* sl = nullptr;
    IPersistFile* pf = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_IShellLinkW, reinterpret_cast<void**>(&sl)))) {
        if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&pf)))
            && SUCCEEDED(pf->Load(reinterpret_cast<LPCOLESTR>(lnkPath.utf16()), STGM_READ))) {
            WCHAR buf[MAX_PATH] = L"";
            WIN32_FIND_DATAW fd{};
            if (SUCCEEDED(sl->GetPath(buf, MAX_PATH, &fd, SLGP_UNCPRIORITY)))
                info.target = QString::fromWCharArray(buf);
            WCHAR argBuf[1024] = L"";
            if (SUCCEEDED(sl->GetArguments(argBuf, 1024)))
                info.args = QString::fromWCharArray(argBuf);
            WCHAR iconBuf[MAX_PATH] = L"";
            int idx = 0;
            if (SUCCEEDED(sl->GetIconLocation(iconBuf, MAX_PATH, &idx)) && iconBuf[0]) {
                info.iconPath = QString::fromWCharArray(iconBuf);
                info.iconIndex = idx;
            }
        }
        pf->Release();
    }
    sl->Release();
    return info;
}

void collectLnkFiles(const QString& dir, int depth, QStringList* out)
{
    if (depth < 0)
        return;
    QDir d(dir);
    if (!d.exists())
        return;
    const auto entries = d.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& fi : entries) {
        if (fi.isDir()) {
            if (!fi.isSymLink()) // 跳过目录联接/符号链接，防止循环
                collectLnkFiles(fi.absoluteFilePath(), depth - 1, out);
        } else if (QString::compare(fi.suffix(), "lnk", Qt::CaseInsensitive) == 0) {
            // 注意：Qt 在 Windows 上把 .lnk 文件视作 symlink，收集时不能用 QDir::NoSymLinks
            out->append(fi.absoluteFilePath());
        }
    }
}

// 单个"应用目录"最多收多少个 exe：有人把层数配大、指到 C:\Windows 那种地方时兜底
constexpr int kMaxExePerDir = 3000;

// 自定义目录里的 exe：depth 是"再往里几层"（0 = 只看这个文件夹自己）。
// 和 collectLnkFiles 同一套约定：目录联接/符号链接不进，防止环。
void collectExeFiles(const QString& dir, int depth, QStringList* out)
{
    if (depth < 0)
        return;
    QDir d(dir);
    if (!d.exists())
        return;
    const auto entries = d.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& fi : entries) {
        if (fi.isDir()) {
            if (!fi.isSymLink())
                collectExeFiles(fi.absoluteFilePath(), depth - 1, out);
        } else if (QString::compare(fi.suffix(), "exe", Qt::CaseInsensitive) == 0) {
            out->append(fi.absoluteFilePath());
        }
    }
}

// 配置页「应用目录」那一项：返回路径列表，层数写在 *depth 里（默认 3 = 往里三层）。
// 列表怎么落到 config.ini 由 QSettings 决定（--appdirs 里用一个带分号的目录名往返验过）。
QStringList customAppDirs(int* depth)
{
    QSettings cfg(Utils::dataDir() + "/config.ini", QSettings::IniFormat);
    if (depth)
        *depth = cfg.value("apps/depth", 3).toInt();
    return cfg.value("apps/roots").toStringList();
}

QString iconCachePng(const QString& iconSrc, int iconIdx)
{
    const QString key = QString::fromUtf8(
        QCryptographicHash::hash((iconSrc + "|" + QString::number(iconIdx)).toUtf8(),
                                 QCryptographicHash::Md5).toHex());
    const QString png = Utils::iconsDir() + "/" + key + ".png";
    if (!QFile::exists(png))
        Utils::extractIconToPng(iconSrc, iconIdx, png);
    return QFile::exists(png) ? png : QString();
}

} // namespace

namespace AppScanner {

QVector<AppEntry> scan()
{
    // 扫描在工作线程跑，这里只解析 .lnk 和抽图标，不碰任何 QWidget
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    struct Loc { QString dir; const char* cat; int depth; };
    QVector<Loc> locs;
    const auto desktop = Utils::knownFolderPath(Utils::KnownFolder::Desktop);
    const auto pubDesktop = Utils::knownFolderPath(Utils::KnownFolder::PublicDesktop);
    const auto userProgs = Utils::knownFolderPath(Utils::KnownFolder::UserStartMenuPrograms);
    const auto commonProgs = Utils::knownFolderPath(Utils::KnownFolder::CommonStartMenuPrograms);
    if (!desktop.isEmpty())
        locs.push_back({desktop, "desktop", 1});
    if (!pubDesktop.isEmpty() && pubDesktop.compare(desktop, Qt::CaseInsensitive) != 0)
        locs.push_back({pubDesktop, "desktop", 0});
    if (!userProgs.isEmpty())
        locs.push_back({userProgs, "startmenu", 10});
    if (!commonProgs.isEmpty())
        locs.push_back({commonProgs, "startmenu", 10});

    QStringList lnks;
    QSet<QString> seen;
    for (const Loc& l : locs) {
        QStringList found;
        collectLnkFiles(l.dir, l.depth, &found);
        for (const QString& f : found) {
            // .lnk 会被 Qt 当 symlink 穿透解析，canonicalFilePath 不可用，用 absoluteFilePath 去重
            const QString key = QFileInfo(f).absoluteFilePath().toLower();
            if (!seen.contains(key)) {
                seen.insert(key);
                lnks.append(f);
            }
        }
    }

    QVector<AppEntry> all;
    all.reserve(lnks.size());
    for (const QString& lnk : lnks) {
        const LinkInfo li = resolveLink(lnk);

        AppEntry e;
        e.name = QFileInfo(lnk).completeBaseName();
        e.lnkPath = lnk;
        e.target = li.target;
        e.args = li.args;
        e.category = lnk.contains("/Microsoft/Windows/Start Menu", Qt::CaseInsensitive)
                         ? "startmenu" : "desktop";

        // 跳过卸载类快捷方式
        const QString nameLower = e.name.toLower();
        if (nameLower.startsWith("uninstall") || e.name.startsWith("卸载")
            || e.target.contains("unins", Qt::CaseInsensitive))
            continue;

        QString iconSrc = li.iconPath;
        int iconIdx = li.iconIndex;
        if (!iconSrc.isEmpty()) {
            iconSrc = Utils::expandEnv(iconSrc);
            if (QDir::isRelativePath(iconSrc)) {
                QString cand = QDir::cleanPath(QFileInfo(lnk).absolutePath() + "/" + iconSrc);
                if (!QFileInfo::exists(cand) && !e.target.isEmpty())
                    cand = QDir::cleanPath(QFileInfo(e.target).absolutePath() + "/" + iconSrc);
                iconSrc = cand;
            }
        }
        if ((iconSrc.isEmpty() || !QFileInfo::exists(iconSrc)) && QFileInfo::exists(e.target)) {
            iconSrc = e.target;
            iconIdx = 0;
        }
        if (!iconSrc.isEmpty() && QFileInfo::exists(iconSrc))
            e.iconFile = iconCachePng(iconSrc, iconIdx);

        all.append(e);
    }

    // ---- 配置页里自己加的应用目录（可以是 exe，也可以是文件夹）----
    // 这些直接进同一份列表、走下面同一套去重，所以开始菜单里已经有的程序不会重复出现。
    int appDepth = 3;
    const QStringList appDirs = customAppDirs(&appDepth);
    int appCollected = 0;
    QElapsedTimer appClock;
    appClock.start();
    for (const QString& raw : appDirs) {
        const QString p = QDir::cleanPath(raw.trimmed());
        if (p.isEmpty())
            continue;
        const QFileInfo fi(p);
        if (!fi.exists()) {
            Utils::log(QString("appscan: 应用目录不存在，跳过 %1").arg(p));
            continue;
        }
        QStringList exes;
        if (fi.isDir())
            collectExeFiles(p, appDepth, &exes);
        else if (QString::compare(fi.suffix(), "exe", Qt::CaseInsensitive) == 0)
            exes.append(fi.absoluteFilePath());
        else
            continue;                       // 指到了别的文件上：不是能启动的东西
        if (exes.size() > kMaxExePerDir) {
            Utils::log(QString("appscan: %1 里找到 %2 个 exe，只收前 %3 个（层数 %4）")
                           .arg(p).arg(exes.size()).arg(kMaxExePerDir).arg(appDepth));
            exes = exes.mid(0, kMaxExePerDir);
        }
        for (const QString& x : exes) {
            const QString base = QFileInfo(x).completeBaseName();
            const QString low = base.toLower();
            if (low.startsWith("unins") || low.contains("uninstall") || base.startsWith("卸载"))
                continue;                   // 卸载器不是要启动的应用，和快捷方式那条规则一致
            AppEntry e;
            e.name = base;
            e.target = x;
            e.category = "folder";
            e.iconFile = iconCachePng(x, 0);
            all.append(e);
            ++appCollected;
        }
    }
    if (!appDirs.isEmpty())
        Utils::log(QString("appscan: 应用目录 %1 个（往里 %2 层）收了 %3 个 exe，用了 %4ms")
                       .arg(appDirs.size()).arg(appDepth).arg(appCollected).arg(appClock.elapsed()));

    // 去重：
    //   目标为空（如 UWP）→ 按名称去重
    //   同一目标的多条 → 只保留一条，优先无参数的（开始菜单副本常带 --scene 之类的内部参数）
    QVector<AppEntry> out;
    out.reserve(all.size());
    QHash<QString, int> bestByTarget;
    QSet<QString> seenNames;
    for (const AppEntry& e : all) {
        if (e.target.isEmpty()) {
            const QString nameKey = "name:" + e.name.toLower();
            if (seenNames.contains(nameKey))
                continue;
            seenNames.insert(nameKey);
            out.append(e);
            continue;
        }
        const QString tkey = QDir::toNativeSeparators(e.target).toLower();
        auto it = bestByTarget.find(tkey);
        if (it == bestByTarget.end()) {
            bestByTarget.insert(tkey, out.size());
            out.append(e);
        } else if (!out[*it].args.isEmpty() && e.args.isEmpty()) {
            out[*it] = e; // 换成无参数的那条
        }
    }

    CoUninitialize();
    return out;
}

QVector<AppEntry> loadIndex()
{
    QVector<AppEntry> out;
    QFile f(Utils::dataDir() + "/apps.json");
    if (!f.open(QIODevice::ReadOnly))
        return out;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isArray())
        return out;
    for (const QJsonValue& v : doc.array()) {
        const QJsonObject o = v.toObject();
        AppEntry e;
        e.name = o.value("name").toString();
        e.target = o.value("target").toString();
        e.args = o.value("args").toString();
        e.lnkPath = o.value("lnk").toString();
        e.iconFile = o.value("icon").toString();
        e.category = o.value("cat").toString();
        if (e.name.isEmpty())
            continue;
        out.append(e);
    }
    return out;
}

void saveIndex(const QVector<AppEntry>& list)
{
    QJsonArray arr;
    for (const AppEntry& e : list) {
        QJsonObject o;
        o.insert("name", e.name);
        o.insert("target", e.target);
        o.insert("args", e.args);
        o.insert("lnk", e.lnkPath);
        o.insert("icon", e.iconFile);
        o.insert("cat", e.category);
        arr.append(o);
    }
    QSaveFile f(Utils::dataDir() + "/apps.json");
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    f.commit();
}

} // namespace AppScanner
