#include "Utils.h"

#include <QClipboard>
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
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPainterPath>
#include <QStandardPaths>
#include <QThread>

#include <QSettings>

#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <psapi.h>

namespace Utils {

QString dataDir()
{
    static QString cached;
    if (cached.isEmpty()) {
        QString base = qEnvironmentVariable("APPDATA");
        if (base.isEmpty())
            base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        cached = base + "/ltool";
    }
    return cached;
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

QString knownFolderPath(KnownFolder kf)
{
    KNOWNFOLDERID id = FOLDERID_Desktop;
    switch (kf) {
    case KnownFolder::Desktop:                  id = FOLDERID_Desktop; break;
    case KnownFolder::PublicDesktop:            id = FOLDERID_PublicDesktop; break;
    case KnownFolder::UserStartMenuPrograms:    id = FOLDERID_Programs; break;
    case KnownFolder::CommonStartMenuPrograms:  id = FOLDERID_CommonPrograms; break;
    }
    PWSTR p = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DONT_VERIFY, nullptr, &p)))
        return {};
    QString r = QString::fromWCharArray(p);
    CoTaskMemFree(p);
    return QDir::fromNativeSeparators(r);
}

bool extractIconToPng(const QString& iconFile, int iconIndex, const QString& pngOut)
{
    QString abs = QDir::toNativeSeparators(QDir::cleanPath(iconFile));
    if (abs.endsWith(".ico", Qt::CaseInsensitive)) {
        QImage img(abs);
        if (!img.isNull())
            return img.save(pngOut);
    }

    HICON hIcon = nullptr;
    UINT n = PrivateExtractIconsW((LPCWSTR)abs.utf16(), iconIndex, 48, 48, &hIcon, nullptr, 1, 0);
    if (n == 0 || hIcon == nullptr)
        return false;
    QImage img = QImage::fromHICON(hIcon);
    DestroyIcon(hIcon);
    if (img.isNull())
        return false;
    return img.save(pngOut);
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

bool launch(const QString& path, const QString& params, const QString& workingDir, bool async)
{
    if (path.isEmpty())
        return false;
    QString file = QDir::toNativeSeparators(path);
    QString wd = workingDir;
    if (wd.isEmpty())
        wd = QFileInfo(path).absolutePath();
    if (!QFileInfo(wd).isDir())
        wd.clear();

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_FLAG_NO_UI;
    if (async)
        sei.fMask |= SEE_MASK_ASYNCOK;   // 交给 shell 异步跑，不等 DDE/进程起来
    sei.lpVerb = L"open";
    sei.lpFile = (LPCWSTR)file.utf16();
    if (!params.isEmpty())
        sei.lpParameters = (LPCWSTR)params.utf16();
    if (!wd.isEmpty()) {
        wd = QDir::toNativeSeparators(wd);
        sei.lpDirectory = (LPCWSTR)wd.utf16();
    }
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != FALSE;
}

void revealInExplorer(const QString& path)
{
    QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    QString params = "/select,\"" + native + "\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", (LPCWSTR)params.utf16(), nullptr, SW_SHOWNORMAL);
}

QString expandEnv(const QString& s)
{
    wchar_t buf[2048];
    DWORD n = ExpandEnvironmentStringsW((LPCWSTR)s.utf16(), buf, 2048);
    if (n == 0 || n > 2048)
        return s;
    QString r = QString::fromWCharArray(buf);
    while (r.endsWith(QChar(0)))
        r.chop(1);
    return r;
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
    if (QFileInfo::exists(expandEnv(s)))
        return s;
    for (int i = s.indexOf(QLatin1Char(' ')); i > 0; i = s.indexOf(QLatin1Char(' '), i + 1)) {
        const QString head = s.left(i);
        if (QFileInfo::exists(expandEnv(head)))
            return head;
    }
    const int sp = s.indexOf(QLatin1Char(' '));
    return sp > 0 ? s.left(sp) : s;
}

bool fileTargetMissing(const QString& raw)
{
    QString p = expandEnv(raw.trimmed());
    p.remove(QLatin1Char('"'));
    if (p.isEmpty())
        return false;                       // 没写过值 ≠ 失效
    if (QFileInfo::exists(p))
        return false;
    if (!p.contains(QLatin1Char('\\')) && !p.contains(QLatin1Char('/'))) {
        wchar_t found[MAX_PATH];
        return SearchPathW(nullptr, (LPCWSTR)p.utf16(), nullptr, MAX_PATH, found, nullptr) == 0;
    }
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

QString fileVersion(const QString& exePath)
{
    const std::wstring w = QDir::toNativeSeparators(exePath).toStdWString();
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(w.c_str(), &handle);
    if (size == 0)
        return {};
    QByteArray data(int(size), 0);
    if (!GetFileVersionInfoW(w.c_str(), 0, size, data.data()))
        return {};
    VS_FIXEDFILEINFO* ffi = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(data.data(), L"\\", (LPVOID*)&ffi, &len) || !ffi)
        return {};
    if (ffi->dwFileVersionMS == 0 && ffi->dwFileVersionLS == 0)
        return {};
    return QString("%1.%2.%3.%4")
        .arg(HIWORD(ffi->dwFileVersionMS))
        .arg(LOWORD(ffi->dwFileVersionMS))
        .arg(HIWORD(ffi->dwFileVersionLS))
        .arg(LOWORD(ffi->dwFileVersionLS));
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

void trimWorkingSet()
{
    const BOOL ok = SetProcessWorkingSetSize(GetCurrentProcess(), SIZE_T(-1), SIZE_T(-1));
    log(QString("trim working set: %1 (剩 %2MB)").arg(ok ? "ok" : QString("failed err %1").arg(GetLastError()))
                   .arg(workingSetMB()));
}

int workingSetMB()
{
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (!K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
        return -1;
    return int(pmc.WorkingSetSize / (1024 * 1024));
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

void setAutostart(bool on)
{
    QSettings run("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                  QSettings::NativeFormat);
    if (on)
        run.setValue("ltool", QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
    else
        run.remove("ltool");
}

bool autostartEnabled()
{
    QSettings run("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                  QSettings::NativeFormat);
    return run.contains("ltool");
}

bool killProcessTree(qint64 pid)
{
    if (pid <= 0)
        return false;
    bool killed = false;
    // 先深度优先杀子孙：winget 会再拉起 AppInstaller/msiexec/setup.exe，
    // 只杀它一个的话安装照样跑完，"取消"就成了假动作
    if (const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W ent{};
        ent.dwSize = sizeof(ent);
        if (Process32FirstW(snap, &ent)) {
            do {
                if ((qint64)ent.th32ProcessID != pid
                    && (qint64)ent.th32ParentProcessID == pid)
                    killed = killProcessTree((qint64)ent.th32ProcessID) || killed;
            } while (Process32NextW(snap, &ent));
        }
        CloseHandle(snap);
    }
    if (const HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid)) {
        killed = TerminateProcess(h, 1602 /* ERROR_CANCELLED */) || killed;
        CloseHandle(h);
    }
    return killed;
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

} // namespace Utils
