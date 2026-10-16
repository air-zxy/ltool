// 平台层第七道缝，Windows 这一份：粉碎的三个动作 + 占用查询 + 结束进程。
// 从 src/FileOps.cpp 原样搬过来（一条判据都不改行为 —— 搬家前后的对照量在
// docs/notes.md 那节，`--shredtest` / `--whohastest [kill]` 跑同一批夹具逐字相同），
// 搬的理由是 src/FileTools.cpp 已经进了可移植名单，链接期要在这道缝上找得到这三个符号。
#include "platform/FileOpsShred.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>

#include <windows.h>
#include <tlhelp32.h>
#include <RestartManager.h>

#include <random>

namespace FileOps {

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

// 粉碎失败那句要说得能用：权限不够就教用户先取消只读，别的错误把 Windows 错误码打出来。
void fillShredError(QString* err)
{
    if (!err)
        return;
    const DWORD e = GetLastError();
    *err = e == ERROR_ACCESS_DENIED
               ? QStringLiteral("没有写权限，覆写不了。先取消只读、或者用管理员运行 ltool")
               : QStringLiteral("粉碎失败（Windows 错误 %1）").arg(e);
}

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
    return FlushFileBuffers(h) != FALSE;
}

} // namespace

bool shredOne(const QString& file, bool thorough, qint64* erased, QString* err)
{
    HANDLE h = CreateFileW(reinterpret_cast<const wchar_t*>(file.utf16()), GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        fillShredError(err);
        return false;
    }
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
        else {
            fillShredError(err);
            return false;
        }
        if (!(DeleteFileW(reinterpret_cast<const wchar_t*>(junk.utf16())) && ok)) {
            fillShredError(err);
            return false;
        }
        return true;
    }
    SetFileAttributesW(reinterpret_cast<const wchar_t*>(file.utf16()), FILE_ATTRIBUTE_NORMAL);
    if (!(DeleteFileW(reinterpret_cast<const wchar_t*>(file.utf16())) && ok)) {
        fillShredError(err);
        return false;
    }
    return true;
}

bool removeEmptyDir(const QString& dir, QString*)
{
    return RemoveDirectoryW(reinterpret_cast<const wchar_t*>(dir.utf16())) != FALSE;
}

qint64 shred(const QString& path, bool thorough, QString* err)
{
    return shredWalk(path, thorough, err);
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
