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
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
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

// 从 PE 头读子系统那两个字节，判断这个 exe 是不是"点一下会有窗口"的程序。
// 布局：0x3C 处 4 字节小端 = PE 头偏移；PE 签名 4 字节 + COFF 头 20 字节 = 可选头起点。
// **Subsystem 在可选头 +68，3 = 控制台（CUI）、2 = GUI** —— 这个偏移不是按记忆写的：
// 本机拿 74 个真 exe 验过（ping/cmd/7z 全在 +68 处读到 3，notepad 那批读到 2），
// 而且 +70 紧跟着就是合法的 DllCharacteristics 掩码（0xc160 / 0x8160），相邻关系对得上。
// 之前按"教科书 +44"写，实现和自造夹具一起错，测试当场白过 —— 别再凭印象写偏移。
// 读不到头（打不开、不是 PE、被截断）按"不是命令行工具"处理，让下面名字那条规则去判 ——
// 别把"我解析失败"变成"什么都不收"。
bool isCommandLineTool(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray head = f.read(1024);
    f.close();
    if (head.size() < 64)
        return false;
    const uchar* b = reinterpret_cast<const uchar*>(head.constData());
    if (b[0] != 'M' || b[1] != 'Z')
        return false;
    const quint32 peOff = quint32(b[60]) | (quint32(b[61]) << 8)
                          | (quint32(b[62]) << 16) | (quint32(b[63]) << 24);
    if (peOff + 4 > quint32(head.size()) || b[peOff] != 'P' || b[peOff + 1] != 'E')
        return false;
    const qint64 subOff = qint64(peOff) + 4 + 20 + 68;
    if (subOff + 1 >= head.size())
        return false;
    const int subsystem = int(b[subOff]) | (int(b[subOff + 1]) << 8);
    return subsystem == 3;
}

// 目录扫出来的东西绝大多数不是"要启动的应用"：这台机器实测把 D:/software 配进去（往里两层）
// 一下捞回 596 个 exe，全是 QQPCTray / BugReport / pg_ctl / 7z / cmake / git-bash /
// fsnotifier / restarter / ShellFolder64 这种子进程和命令行工具，把搜索框彻底淹了。
// 两条判据，命中任一条就不算应用：控制台子系统、名字或路径像助手。
// 误杀的出路是配置那一栏里**直接写那个 exe 的完整路径** —— 点名加的不走过滤，无条件收。
bool looksLikeHelper(const QString& exePath)
{
    static const QStringList nameBits = {
        QStringLiteral("updater"), QStringLiteral("autoupdate"), QStringLiteral("upgrad"),
        QStringLiteral("bugreport"), QStringLiteral("crash"), QStringLiteral("helper"),
        QStringLiteral("render"), QStringLiteral("elevator"), QStringLiteral("restarter"),
        QStringLiteral("service"), QStringLiteral("daemon"), QStringLiteral("agent"),
        QStringLiteral("collector"), QStringLiteral("diagnos"), QStringLiteral("notifier"),
        QStringLiteral("watcher"), QStringLiteral("bridge"), QStringLiteral("unins"),
        QStringLiteral("installer"), QStringLiteral("patch"), QStringLiteral("debug"),
        QStringLiteral("console"), QStringLiteral("cli"), QStringLiteral("bash"),
    };
    const QString base = QFileInfo(exePath).completeBaseName().toLower();
    for (const QString& bit : nameBits)
        if (base.contains(bit))
            return true;
    static const QStringList dirBits = {
        QStringLiteral("locales"), QStringLiteral("swiftshader"), QStringLiteral("plugins"),
        QStringLiteral("redist"), QStringLiteral("vcruntime"), QStringLiteral("helpers"),
        QStringLiteral("x86"), QStringLiteral("x64"), QStringLiteral("i386"),
        QStringLiteral("arm64"), QStringLiteral("win32"), QStringLiteral("win64"),
    };
    static const QRegularExpression versionDir(QStringLiteral("^v?[0-9]+(\.[0-9]+)+$"));
    const QStringList parts = QDir::fromNativeSeparators(exePath).toLower()
                                  .split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (int i = 0; i + 1 < parts.size(); ++i) {      // 最后一段是文件本身，名字归上面管
        const QString& seg = parts[i];
        for (const QString& bit : dirBits)
            if (seg == bit)
                return true;
        if (versionDir.match(seg).hasMatch())
            return true;                              // 18.1.30302.212 / 3.12 这种版本目录
    }
    return false;
}

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
    int appFiltered = 0;
    QElapsedTimer appClock;
    appClock.start();
    // "这个目录里已经有主程序了"的清单：桌面/开始菜单的快捷方式指向哪个 exe，那个目录就算被占；
    // 用户点名加进来的 exe 同理。目录扫出来的别的 exe 就不再自动收 ——
    // 实测这条杀的是 QQEX.exe（QQ 的协议启动器，双击没反应），而 QQ.exe 有桌面快捷方式、留着。
    auto dirKey = [](const QString& exePath) {
        return QDir::fromNativeSeparators(QFileInfo(exePath).absolutePath()).toLower();
    };
    QSet<QString> claimedDirs;
    for (const AppEntry& e : all)
        if (!e.target.isEmpty() && e.target.endsWith(".exe", Qt::CaseInsensitive))
            claimedDirs.insert(dirKey(e.target));
    for (const QString& raw : appDirs) {
        const QFileInfo fi(QDir::cleanPath(raw.trimmed()));
        if (fi.isFile() && QString::compare(fi.suffix(), "exe", Qt::CaseInsensitive) == 0)
            claimedDirs.insert(dirKey(fi.absoluteFilePath()));
    }
    int appShadowed = 0;
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
        // 直接点名那个 exe = 用户就要它，不跟目录来源一样过滤
        const bool autoScan = fi.isDir();
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
            if (autoScan && (isCommandLineTool(x) || looksLikeHelper(x))) {
                ++appFiltered;              // 子进程和命令行工具：搜索框里没用，还占着前排
                continue;
            }
            // 同目录已经有"人点的那个"了（快捷方式指向它，或你点名加过它）→ 这个不再自动收。
            // 那个主程序本身也不在这儿收：它已经以快捷方式的名字在列表里了（下面还会去重一次）
            if (autoScan && claimedDirs.contains(dirKey(x))) {
                ++appShadowed;
                continue;
            }
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
        Utils::log(QString("appscan: 应用目录 %1 个（往里 %2 层）收了 %3 个应用，"
                           "跳过 %4 个不像主程序的、%5 个同目录已有主程序的，用了 %6ms")
                       .arg(appDirs.size()).arg(appDepth).arg(appCollected).arg(appFiltered)
                       .arg(appShadowed).arg(appClock.elapsed()));

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
