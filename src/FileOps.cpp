#include "FileOps.h"

#include "Utils.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QHash>
#include <QFileInfo>
#include <QSet>

#include <windows.h>
#include <winternl.h>
#include <tlhelp32.h>
#include <RestartManager.h>

#include <random>

namespace {



QString processNameOf(DWORD pid)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return {};
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    QString name;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == pid) {
                name = QString::fromWCharArray(pe.szExeFile);
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return name;
}

QString processExeOf(DWORD pid)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return {};
    wchar_t buf[1024];
    DWORD len = 1024;
    QString path;
    if (QueryFullProcessImageNameW(h, 0, buf, &len))
        path = QString::fromWCharArray(buf, int(len));
    CloseHandle(h);
    return path;
}

// "File" 这个类型在句柄表里的编号。
// 不能按固定结构去解析 ObjectTypesInformation —— 那个表每一项都是变长的，
// 猜偏移会直接读到野指针。改成自己开一个真文件句柄，回到句柄表里找它自己，
// 它那一行的 ObjectTypeIndex 就是 File 类型的编号。
bool fillRandom(HANDLE h, qint64 bytes)
{
    std::mt19937_64 rng(std::random_device{}());
    QByteArray chunk(1 << 20, 0);
    qint64 left = bytes;
    while (left > 0) {
        const DWORD take = DWORD(qMin<qint64>(left, chunk.size()));
        for (DWORD i = 0; i + 8 <= take; i += 8)
            *reinterpret_cast<quint64*>(chunk.data() + i) = rng();
        for (DWORD i = take - (take % 8); i < take; ++i)
            chunk[i] = char(rng() & 0xFF);
        DWORD done = 0;
        if (!WriteFile(h, chunk.constData(), take, &done, nullptr) || done != take)
            return false;
        left -= take;
    }
    return FlushFileBuffers(h);
}

bool shredOne(const QString& file, bool thorough, qint64* erased)
{
    HANDLE h = CreateFileW(reinterpret_cast<const wchar_t*>(file.utf16()), GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    bool ok = true;
    const int passes = thorough ? 3 : 1;
    for (int pass = 0; pass < passes && ok; ++pass) {
        if (pass > 0)
            SetFilePointerEx(h, LARGE_INTEGER{}, nullptr, FILE_BEGIN);
        if (pass == 1) {                       // 中间一遍全 0
            QByteArray zero(1 << 20, 0);
            qint64 left = sz.QuadPart;
            while (left > 0) {
                const DWORD take = DWORD(qMin<qint64>(left, zero.size()));
                DWORD done = 0;
                if (!WriteFile(h, zero.constData(), take, &done, nullptr) || done != take) { ok = false; break; }
                left -= take;
            }
        } else if (!fillRandom(h, sz.QuadPart)) {
            ok = false;
        }
        FlushFileBuffers(h);
    }
    // 截断：让"文件大小"这个元信息也不指向原来的数据块
    SetFilePointerEx(h, LARGE_INTEGER{}, nullptr, FILE_BEGIN);
    SetEndOfFile(h);
    CloseHandle(h);
    if (ok)
        *erased += sz.QuadPart;

    if (thorough) {                            // 连名字（可能带原始文件名）也抹掉
        const QFileInfo fi(file);
        const QString junk = fi.dir().absoluteFilePath(
            QStringLiteral("~%1.tmp").arg(quint64(std::random_device{}()) * 2654435761ULL, 12, 16, QLatin1Char('0')));
        if (MoveFileW(reinterpret_cast<const wchar_t*>(file.utf16()),
                      reinterpret_cast<const wchar_t*>(junk.utf16())))
            SetFileAttributesW(reinterpret_cast<const wchar_t*>(junk.utf16()), FILE_ATTRIBUTE_NORMAL);
        else
            return false;
        return DeleteFileW(reinterpret_cast<const wchar_t*>(junk.utf16())) && ok;
    }
    SetFileAttributesW(reinterpret_cast<const wchar_t*>(file.utf16()), FILE_ATTRIBUTE_NORMAL);
    return DeleteFileW(reinterpret_cast<const wchar_t*>(file.utf16())) && ok;
}

} // namespace

namespace FileOps {

QString human(qint64 bytes)
{
    if (bytes < 0)
        return QStringLiteral("—");
    static const char* u[] = { "B", "KB", "MB", "GB", "TB" };
    double v = double(bytes);
    int i = 0;
    while (v >= 1024.0 && i < 4) { v /= 1024.0; ++i; }
    return i == 0 ? QStringLiteral("%1 B").arg(bytes)
                  : QStringLiteral("%1 %2").arg(v, 0, 'f', v < 10 ? 2 : 1).arg(QString::fromUtf8(u[i]));
}

Info info(const QString& path)
{
    Info out;
    out.path = QDir::toNativeSeparators(path);
    const QFileInfo fi(path);
    if (!fi.exists()) {
        out.error = QStringLiteral("文件不存在");
        return out;
    }
    out.isDir = fi.isDir();
    const std::wstring w = out.path.toStdWString();
    out.size = out.isDir ? 0 : fi.size();
    out.lastWrite = fi.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));

    wchar_t root[4] = { w[0], L':', L'\\', 0 };
    DWORD spc = 0, bpc = 0, total = 0, freeC = 0;
    if (GetDiskFreeSpaceW(root, &spc, &bpc, &total, &freeC) && spc && bpc) {
        out.cluster = qint64(spc) * qint64(bpc);
    }
    wchar_t fs[64] = {};
    if (GetVolumeInformationW(root, nullptr, 0, nullptr, nullptr, nullptr, fs, 64))
        out.fsName = QString::fromWCharArray(fs);

    const DWORD attr = GetFileAttributesW(w.c_str());
    out.sparse = (attr & FILE_ATTRIBUTE_SPARSE_FILE) != 0;
    out.compressed = (attr & FILE_ATTRIBUTE_COMPRESSED) != 0;
    out.encrypted = (attr & FILE_ATTRIBUTE_ENCRYPTED) != 0;
    QStringList a;
    if (attr & FILE_ATTRIBUTE_READONLY) a << QStringLiteral("只读");
    if (attr & FILE_ATTRIBUTE_HIDDEN) a << QStringLiteral("隐藏");
    if (attr & FILE_ATTRIBUTE_SYSTEM) a << QStringLiteral("系统");
    if (out.compressed) a << QStringLiteral("NTFS 压缩");
    if (out.sparse) a << QStringLiteral("稀疏");
    if (out.encrypted) a << QStringLiteral("EFS 加密");
    out.attrs = a.join(QStringLiteral(" · "));

    // 实际占的簇：压缩/稀疏文件这个值才小于逻辑大小
    LARGE_INTEGER alloc{};
    HANDLE h = CreateFileW(w.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        FILE_STANDARD_INFO si{};
        if (GetFileInformationByHandleEx(h, FileStandardInfo, &si, sizeof(si)))
            out.hardLinks = int(si.NumberOfLinks);
        BY_HANDLE_FILE_INFORMATION bh{};
        if (GetFileInformationByHandle(h, &bh))
            out.hardLinks = int(bh.nNumberOfLinks);
        if (GetFileSizeEx(h, &alloc))
            ;                                  // 逻辑大小已经有了
        CloseHandle(h);
    }
    if (!out.isDir) {
        DWORD hi = 0;
        const DWORD lo = GetCompressedFileSizeW(w.c_str(), &hi);
        if (lo != INVALID_FILE_ATTRIBUTES || GetLastError() == ERROR_SUCCESS) {
            out.alloc = (qint64(hi) << 32) | lo;
        }
        if (out.alloc <= 0)
            out.alloc = out.cluster > 0 ? ((out.size + out.cluster - 1) / out.cluster) * out.cluster : out.size;
    }

    out.ok = true;
    return out;
}

Usage usage(const QString& path)
{
    Usage out;
    const QFileInfo fi(path);
    if (!fi.exists()) {
        out.error = QStringLiteral("文件不存在");
        return out;
    }
    const auto addFile = [&out](const QString& native) {
        const std::wstring w = native.toStdWString();
        WIN32_FILE_ATTRIBUTE_DATA d{};
        if (GetFileAttributesExW(w.c_str(), GetFileExInfoStandard, &d))
            out.bytes += (qint64(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
        DWORD hi = 0;
        const DWORD lo = GetCompressedFileSizeW(w.c_str(), &hi);
        if (lo != INVALID_FILE_ATTRIBUTES || GetLastError() == ERROR_SUCCESS)
            out.alloc += (qint64(hi) << 32) | lo;
        ++out.files;
    };

    if (!fi.isDir()) {
        addFile(QDir::toNativeSeparators(path));
        out.ok = true;
        return out;
    }
    QDirIterator fit(path, QDir::Files | QDir::NoSymLinks | QDir::Hidden | QDir::System,
                     QDirIterator::Subdirectories);
    while (fit.hasNext()) {
        fit.next();
        addFile(QDir::toNativeSeparators(fit.filePath()));
    }
    QDirIterator dit(path, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                     QDirIterator::Subdirectories);
    while (dit.hasNext()) {
        dit.next();
        ++out.dirs;
    }
    out.ok = true;
    return out;
}

qint64 shred(const QString& path, bool thorough, QString* err)
{
    const QFileInfo fi(path);
    if (!fi.exists()) {
        if (err)
            *err = QStringLiteral("文件不存在：%1").arg(path);
        return -1;
    }
    qint64 erased = 0;
    if (fi.isDir()) {
        QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks,
                        QDirIterator::Subdirectories);
        QStringList files;
        while (it.hasNext())
            files << it.next();
        for (const QString& f : files) {
            qint64 sub = 0;
            if (!shredOne(f, thorough, &sub)) {
                if (err)
                    *err = QStringLiteral("这个文件没能覆写删除（可能被占用或没有写权限）：%1").arg(f);
                return erased > 0 ? erased : -1;
            }
            erased += sub;
        }
        // 从最深的目录往上删
        QDirIterator dit(path, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                         QDirIterator::Subdirectories);
        QStringList dirs;
        while (dit.hasNext())
            dirs << dit.next();
        std::sort(dirs.begin(), dirs.end(), [](const QString& a, const QString& b) {
            return a.size() > b.size();
        });
        dirs << QDir::toNativeSeparators(path);
        for (const QString& d : dirs) {
            if (!RemoveDirectoryW(reinterpret_cast<const wchar_t*>(d.utf16())) && err)
                *err = QStringLiteral("目录删不掉（里面可能还有别人刚写进去的东西）：%1").arg(d);
        }
        return erased;
    }
    if (!shredOne(QDir::toNativeSeparators(path), thorough, &erased)) {
        const DWORD e = GetLastError();
        if (err)
            *err = e == ERROR_ACCESS_DENIED
                       ? QStringLiteral("没有写权限，覆写不了。先取消只读、或者用管理员运行 ltool")
                       : QStringLiteral("粉碎失败（Windows 错误 %1）").arg(e);
        return -1;
    }
    return erased;
}

// 谁开着这个文件：用 Windows 的 Restart Manager（rstrtmgr）—— 它就是为这个查询设计的，
// 毫秒级返回。以前自己遍历系统句柄表的做法要给每个文件句柄 DuplicateHandle + NtQueryObject，
// 实测在这台机器上跑几分钟都出不来，还会卡在管道句柄上。
QVector<Holder> whoHas(const QString& path, QString* err)
{
    QVector<Holder> out;
    const QString native = QDir::toNativeSeparators(path);
    std::wstring w = native.toStdWString();
    DWORD session = 0;
    wchar_t key[CCH_RM_SESSION_KEY + 1] = {};
    UINT rc = RmStartSession(&session, 0, key);
    if (rc != ERROR_SUCCESS) {
        if (err)
            *err = QStringLiteral("启动 Restart Manager 会话失败（%1）").arg(rc);
        return out;
    }
    LPCWSTR targets[1] = { w.c_str() };
    rc = RmRegisterResources(session, 1, targets, 0, nullptr, 0, nullptr);
    if (rc != ERROR_SUCCESS) {
        if (err)
            *err = QStringLiteral("登记要查的文件失败（%1）").arg(rc);
        RmEndSession(session);
        return out;
    }
    // 先问要几个，再取（rgAffectedApps 传 nullptr 就是量大小）
    UINT needed = 0, filled = 0;
    DWORD reasons = 0;
    rc = RmGetList(session, &needed, &filled, nullptr, &reasons);
    if (rc != ERROR_SUCCESS && rc != ERROR_MORE_DATA && rc != ERROR_INVALID_PARAMETER) {
        if (err)
            *err = QStringLiteral("查询占用进程失败（%1）").arg(rc);
        RmEndSession(session);
        return out;
    }
    if (needed == 0) {
        RmEndSession(session);
        return out;                        // 确实没人开着
    }
    QVector<RM_PROCESS_INFO> infos;          // 分两行写：`infos(size_t(needed))` 会被当成函数声明
    infos.resize(int(needed));
    filled = needed;
    reasons = 0;
    rc = RmGetList(session, &needed, &filled, infos.data(), &reasons);
    RmEndSession(session);
    if (rc != ERROR_SUCCESS) {
        if (err)
            *err = QStringLiteral("取占用进程列表失败（%1）").arg(rc);
        return out;
    }
    QHash<quint64, Holder> byPid;
    for (UINT i = 0; i < filled; ++i) {
        const RM_PROCESS_INFO& pi = infos[int(i)];
        const quint64 pid = quint64(pi.Process.dwProcessId);
        Holder& hh = byPid[pid];
        if (hh.pid == 0) {
            hh.pid = pid;
            hh.processName = QString::fromWCharArray(pi.strAppName).trimmed();
            if (hh.processName.isEmpty())
                hh.processName = processNameOf(DWORD(pid));
            hh.exePath = processExeOf(DWORD(pid));
        }
        ++hh.handles;
        const QString svc = QString::fromWCharArray(pi.strServiceShortName).trimmed();
        if (!svc.isEmpty() && hh.objects.size() < 4)
            hh.objects << svc;
        if (!pi.bRestartable && hh.objects.size() < 4)
            hh.objects << QStringLiteral("该进程不可自动重启");
    }
    for (auto& h : byPid)
        out.append(h);
    std::sort(out.begin(), out.end(), [](const Holder& a, const Holder& b) {
        return a.handles > b.handles;
    });
    return out;
}

bool killProcess(quint64 pid, QString* err)
{
    HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, DWORD(pid));
    if (!h) {
        const DWORD e = GetLastError();
        if (err)
            *err = e == ERROR_ACCESS_DENIED
                       ? QStringLiteral("结束不了：权限不够（那个进程可能是管理员或系统账户起的）")
                       : QStringLiteral("打不开那个进程（Windows 错误 %1）").arg(e);
        return false;
    }
    const BOOL ok = TerminateProcess(h, 0x41);
    if (!ok && err)
        *err = QStringLiteral("TerminateProcess 失败（Windows 错误 %1）").arg(GetLastError());
    CloseHandle(h);
    return ok != FALSE;
}

} // namespace FileOps
