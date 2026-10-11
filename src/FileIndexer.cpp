#include "FileIndexer.h"
#include "UsnIndex.h"
#include "Utils.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QSaveFile>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>

#include <algorithm>
#include <cstring>
#include <thread>

#include <windows.h>
#include <winioctl.h>

namespace {

// 快索引 + 深索引共用的跳过名单（目录名，忽略大小写）
const QStringList& skipDirNames()
{
    static const QStringList names = {
        "AppData", "Application Data", "$Recycle.Bin", "System Volume Information",
        ".git", ".svn", ".vs", ".idea", "node_modules", "__pycache__",
        ".venv", "venv", "site-packages", ".cargo", ".rustup", ".gradle", ".m2",
        "WinSxS", "WindowsApps", "Installer", "servicing", "WinRE", "Panther",
        "target", "CMakeFiles",
    };
    return names;
}

bool shouldSkip(const QString& name)
{
    for (const QString& s : skipDirNames())
        if (name.compare(s, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

// ASCII 大小写无关查找（UTF-8 多字节序列按原字节精确比较，中文不受影响）
int indexOfCI(const char* hay, int hayLen, const char* needle, int needleLen)
{
    if (needleLen == 0)
        return 0;
    if (needleLen > hayLen)
        return -1;
    for (int i = 0; i + needleLen <= hayLen; ++i) {
        int j = 0;
        for (; j < needleLen; ++j) {
            if (tolower((unsigned char)hay[i + j]) != tolower((unsigned char)needle[j]))
                break;
        }
        if (j == needleLen)
            return i;
    }
    return -1;
}

void walk(const QString& dir, int depth, QVector<FileEntry>* out, int cap)
{
    if (depth < 0 || out->size() >= cap)
        return;
    QDir d(dir);
    const auto list = d.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const QFileInfo& fi : list) {
        const QString fn = fi.fileName();
        if (shouldSkip(fn))
            continue;
        if (out->size() >= cap)
            return;
        out->append({fn, fi.absoluteFilePath(), fi.isDir()});
        if (fi.isDir())
            walk(fi.absoluteFilePath(), depth - 1, out, cap);
    }
}

// 目录遍历全量构建一个卷的深索引（无管理员权限时的回退；frn 不可用）
// 递归写成普通函数而不是 std::function + [&] 捕获：开机那次崩溃（0xc0000005）就落在
// 这个闭包的捕获槽上（见 CrashDumps/ltool.exe.12504.dmp，栈里 15 层同一个 lambda 帧），
// 按引用捕获的 this 指针被写坏后没有任何编译期约束；普通函数的参数走寄存器，也不吃
// std::function 的类型擦除开销。
void walkDeep(const QString& dir, int depth, QVector<FileDeepEntry>* entries, QByteArray* arena)
{
    if (depth < 0 || entries->size() >= 5000000)
        return;
    QDir d(dir);
    const auto list = d.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const QFileInfo& fi : list) {
        const QString fn = fi.fileName();
        if (shouldSkip(fn))
            continue;
        if (entries->size() >= 5000000)
            return;
        const QString path = QDir::fromNativeSeparators(fi.absoluteFilePath());
        FileDeepEntry e;
        e.offset = quint32(arena->size());
        arena->append(path.toUtf8());
        e.len = quint32(arena->size() - e.offset);
        e.nameStart = e.len - quint32(fn.toUtf8().size());
        e.isDir = fi.isDir() ? 1 : 0;
        entries->append(e);
        if (fi.isDir())
            walkDeep(path, depth - 1, entries, arena);
    }
}

void walkDeepVolume(const QString& root, QVector<FileDeepEntry>* entries, QByteArray* arena)
{
    walkDeep(root, 32, entries, arena);
}

// 所有固定磁盘根（不含光驱/网络盘）
QStringList fixedDriveRoots()
{
    QStringList roots;
    for (const QStorageInfo& si : QStorageInfo::mountedVolumes()) {
        if (!si.isReady() || si.device().isEmpty())
            continue;
        const QString root = si.rootPath();
        if (root.size() < 2 || root[1] != ':')
            continue;
        const UINT type = GetDriveTypeW((LPCWSTR)QDir::toNativeSeparators(root).utf16());
        if (type == DRIVE_FIXED)
            roots.append(root);
    }
    return roots;
}

// 映射视图里的一条打包条目（21 字节，与 deepindex.dat 的存储布局一致）
#pragma pack(push, 1)
struct PackedDeepEntry {
    quint32 offset;
    quint32 len;
    quint32 nameStart;
    quint8 isDir;
    quint64 frn;
};
#pragma pack(pop)

constexpr int kPackedEntrySize = 21;

// ---------- 实时监听（快索引） ----------

constexpr DWORD kQuickWatchBuffer = 64 * 1024;

struct WatchRoot {
    QString root;           // 原生分隔符
    HANDLE dir = nullptr;
    HANDLE event = nullptr;
    OVERLAPPED ov {};
    QByteArray buffer;
};

struct WatchCtx {
    HANDLE stopEvent = nullptr;
    QVector<WatchRoot> roots;
};

bool issueWatch(WatchRoot& r)
{
    ResetEvent(r.event);
    ZeroMemory(&r.ov, sizeof(r.ov));
    r.ov.hEvent = r.event;
    DWORD bytes = 0;
    return ReadDirectoryChangesW(r.dir, r.buffer.data(), DWORD(r.buffer.size()), TRUE,
                                 FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME,
                                 &bytes, &r.ov, nullptr) != 0;
}

// 路径是否属于可索引范围（不含被跳过的组件、不含 $ 前缀组件）
bool pathIndexable(const QString& path)
{
    const QStringList parts = path.split('/', Qt::SkipEmptyParts);
    for (const QString& p : parts) {
        if (p.compare("C:", Qt::CaseInsensitive) == 0
            || (p.size() == 2 && p[1] == ':'))
            continue;
        if (shouldSkip(p) || p.startsWith('$'))
            return false;
    }
    return true;
}

} // namespace

FileIndexer& FileIndexer::instance()
{
    static FileIndexer inst;
    return inst;
}

int FileIndexer::count() const
{
    QReadLocker lock(&m_lock);
    return m_entries.size();
}

int FileIndexer::deepCount() const
{
    QReadLocker lock(&m_lock);
    int n = 0;
    for (const MappedVol& v : m_base)
        n += int(v.count);
    n -= m_removed.size();
    n += m_add.size();
    return qMax(0, n);
}

QVector<FileIndexer::QuickRoot> FileIndexer::quickRoots()
{
    QVector<QuickRoot> roots;
    const auto add = [&roots](const QString& dir, int depth) {
        if (!dir.isEmpty())
            roots.append({dir, depth});
    };
    add(Utils::knownFolderPath(Utils::KnownFolder::Desktop), 4);
    add(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), 3);
    add(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation), 3);
    add(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation), 2);
    add(QStandardPaths::writableLocation(QStandardPaths::MusicLocation), 2);
    add(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation), 2);
    add(QStandardPaths::writableLocation(QStandardPaths::HomeLocation), 1);
    return roots;
}

void FileIndexer::reindexAsync(bool forceDeep)
{
    if (!m_running.testAndSetRelaxed(0, 1))
        return;

    auto future = QtConcurrent::run([this, forceDeep]() -> int {
        QElapsedTimer timer;
        timer.start();

        QSettings cfg(Utils::dataDir() + "/config.ini", QSettings::IniFormat);
        const bool deepEnabled = cfg.value("index/deep", true).toBool();

        // 1) 深索引：映射磁盘上的快照（不占私有内存），启动立即可搜
        bool haveCache = false;
        if (deepEnabled)
            haveCache = mapSnapshot();

        // 2) 快索引全量
        QVector<FileEntry> quick;
        quick.reserve(8192);
        for (const QuickRoot& r : quickRoots())
            walk(r.dir, r.depth, &quick, 200000);
        {
            QWriteLocker lock(&m_lock);
            m_entries = quick;
        }

        // 3) 启动常用目录实时监听（新增/删除秒级反映）
        startWatchers();

        // 4) 深索引更新：USN 增量优先（管理员 + 有状态），否则全量重建
        if (deepEnabled) {
            const DeepStatus st = (haveCache && !forceDeep)
                ? incrementalDeep() : DeepStatus::NeedRebuild;
            if (st == DeepStatus::Ok) {
                Utils::log(QString("deep index incremental ok in %1ms (quick=%2 deep=%3)")
                               .arg(timer.elapsed()).arg(m_entries.size()).arg(deepCount()));
            } else if (st == DeepStatus::NoAccess && !forceDeep && !cacheTooOld()) {
                Utils::log(QString("deep index: no admin rights, keep snapshot "
                                   "(quick=%1 deep=%2)").arg(m_entries.size()).arg(deepCount()));
            } else {
                QElapsedTimer t2;
                t2.start();
                rebuildDeepAll();
                Utils::log(QString("deep index full rebuild in %1s (quick=%2 deep=%3)")
                               .arg(t2.elapsed() / 1000).arg(m_entries.size()).arg(deepCount()));
            }
        }

        int total = 0;
        {
            QReadLocker lock(&m_lock);
            total = m_entries.size();
            for (const MappedVol& v : m_base)
                total += int(v.count);
            total += m_add.size();
            total -= qMin<qint64>(m_removed.size(), total > 0 ? total : 0);
        }
        return total;
    });

    auto* watcher = new QFutureWatcher<int>(this);
    connect(watcher, &QFutureWatcher<int>::finished, this, [this, watcher] {
        m_running.storeRelaxed(0);
        emit indexingFinished(watcher->result());
        watcher->deleteLater();

        // 定时刷新：增量路径毫秒级；无管理员权限时为一次后台全量重扫
        QSettings cfg(Utils::dataDir() + "/config.ini", QSettings::IniFormat);
        const int hours = cfg.value("index/refreshHours", 6).toInt();
        if (hours > 0) {
            QTimer::singleShot(int(hours) * 3600 * 1000, this, [this] {
                if (m_running.loadRelaxed() == 0)
                    reindexAsync();
            });
        }
    });
    watcher->setFuture(future);
}

// ---------- 深索引：映射快照 ----------

void FileIndexer::unmapSnapshot()
{
    if (m_mapView)
        UnmapViewOfFile(m_mapView);
    if (m_mapObj)
        CloseHandle(m_mapObj);
    if (m_mapFile && m_mapFile != INVALID_HANDLE_VALUE)
        CloseHandle((HANDLE)m_mapFile);
    m_mapView = nullptr;
    m_mapObj = nullptr;
    m_mapFile = nullptr;
    m_mapSize = 0;
    m_base.clear();
}

bool FileIndexer::mapSnapshot()
{
    unmapSnapshot();
    const QString path = Utils::dataDir() + "/deepindex.dat";
    HANDLE file = CreateFileW((LPCWSTR)QDir::toNativeSeparators(path).utf16(),
                              GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(file, &sz) || sz.QuadPart < 9) {
        CloseHandle(file);
        return false;
    }
    HANDLE mapObj = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!mapObj) {
        CloseHandle(file);
        return false;
    }
    void* view = MapViewOfFile(mapObj, FILE_MAP_READ, 0, 0, 0);
    if (!view) {
        CloseHandle(mapObj);
        CloseHandle(file);
        return false;
    }

    // 解析卷头（只读头信息，数据本体留在映射视图里）
    const char* p = (const char*)view;
    const char* end = (const char*)view + sz.QuadPart;
    auto readU32 = [&](quint32* out) {
        if (p + 4 > end) return false;
        memcpy(out, p, 4); p += 4; return true;
    };
    auto readU64 = [&](quint64* out) {
        if (p + 8 > end) return false;
        memcpy(out, p, 8); p += 8; return true;
    };

    QVector<MappedVol> base;
    quint32 volCount = 0;
    bool ok = memcmp(p, "LTDI2", 5) == 0;
    if (ok) {
        p += 5;
        ok = readU32(&volCount) && volCount <= 64;
    }
    if (ok) {
        for (quint32 vi = 0; vi < volCount && ok; ++vi) {
            quint32 rootLen = 0;
            if (!readU32(&rootLen) || p + rootLen > end) { ok = false; break; }
            MappedVol v;
            v.root = QString::fromUtf8(p, int(rootLen));
            p += rootLen;
            if (!readU64(&v.journalId) || !readU64(&v.nextUsn)) { ok = false; break; }
            quint32 count = 0, arenaLen = 0;
            if (!readU32(&count) || !readU32(&arenaLen) || count > 20000000u) { ok = false; break; }
            const qint64 need = qint64(count) * kPackedEntrySize + arenaLen;
            if (p + need > end) { ok = false; break; }
            v.entryOff = quint64(p - (const char*)view);
            v.arenaOff = v.entryOff + quint64(count) * kPackedEntrySize;
            v.count = count;
            v.arenaLen = arenaLen;
            p += need;
            base.append(v);
        }
    }
    if (!ok) {
        UnmapViewOfFile(view);
        CloseHandle(mapObj);
        CloseHandle(file);
        return false;
    }

    m_mapFile = file;
    m_mapObj = mapObj;
    m_mapView = view;
    m_mapSize = sz.QuadPart;
    m_base = base;
    loadUsnState();
    return true;
}


// ---------- 深索引：全量重建 ----------

void FileIndexer::loadUsnState()
{
    QFile f(Utils::dataDir() + "/usnstate.dat");
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QByteArray all = f.readAll();
    f.close();
    if (all.size() < 9 || memcmp(all.constData(), "LTSU1", 5) != 0)
        return;
    const char* p = all.constData() + 5;
    const char* end = all.constData() + all.size();
    auto readU32 = [&](quint32* out) {
        if (p + 4 > end) return false;
        memcpy(out, p, 4); p += 4; return true;
    };
    auto readU64 = [&](quint64* out) {
        if (p + 8 > end) return false;
        memcpy(out, p, 8); p += 8; return true;
    };
    quint32 volCount = 0;
    if (!readU32(&volCount) || volCount > 64)
        return;
    for (quint32 i = 0; i < volCount; ++i) {
        quint32 rootLen = 0;
        if (!readU32(&rootLen) || p + rootLen > end)
            return;
        const QString root = QString::fromUtf8(p, int(rootLen));
        p += rootLen;
        quint64 jid = 0, usn = 0;
        if (!readU64(&jid) || !readU64(&usn))
            return;
        for (VolRuntime& v : m_vols) {
            if (v.root.compare(root, Qt::CaseInsensitive) == 0) {
                v.journalId = jid;
                v.nextUsn = usn;
                break;
            }
        }
    }
}

void FileIndexer::saveUsnState()
{
    QSaveFile f(Utils::dataDir() + "/usnstate.dat");
    if (!f.open(QIODevice::WriteOnly))
        return;
    QByteArray out;
    out.append("LTSU1", 5);
    const auto put32 = [&out](quint32 x) { char b[4]; memcpy(b, &x, 4); out.append(b, 4); };
    const auto put64 = [&out](quint64 x) { char b[8]; memcpy(b, &x, 8); out.append(b, 8); };
    put32(quint32(m_vols.size()));
    for (const VolRuntime& v : m_vols) {
        const QByteArray rootUtf8 = v.root.toUtf8();
        put32(quint32(rootUtf8.size()));
        out.append(rootUtf8);
        put64(v.journalId);
        put64(v.nextUsn);
    }
    f.write(out);
    f.commit();
}

void FileIndexer::rebuildDeepAll()
{
    const QStringList roots = fixedDriveRoots();

    // 构建临时卷数据（MFT 优先，失败回退目录遍历）
    struct FreshVol {
        QString root;
        QVector<FileDeepEntry> entries;
        QByteArray arena;
        quint64 journalId = 0, nextUsn = 0;
    };
    QVector<FreshVol> fresh;
    fresh.reserve(roots.size());

    for (const QString& root : roots) {
        FreshVol v;
        v.root = root;

        UsnIndex::VolumeHandle vol;
        UsnIndex::EnumResult er;
        bool mftOk = false;
        if (vol.open(root)) {
            mftOk = UsnIndex::enumVolumeMft(vol.vol, er);
            if (!mftOk)
                Utils::log(QString("MFT enum failed for %1 err=%2").arg(root).arg(GetLastError()));
            vol.close();
        } else {
            Utils::log(QString("volume open failed for %1 err=%2 (回退目录遍历)")
                           .arg(root).arg(GetLastError()));
        }

        if (mftOk) {
            // 由 FRN 父链组合完整路径（memoized）
            struct PathInfo { int off = -1; int len = 0; int nameLen8 = 0; bool emitFlag = false; bool done = false; };
            const int n = er.frn.size();
            QVector<PathInfo> info(n);
            QHash<quint64, int> byFrn;
            byFrn.reserve(n * 2);
            for (int i = 0; i < n; ++i)
                byFrn.insert(er.frn[i], i);

            std::function<void(int)> compose = [&](int i) {
                if (info[i].done)
                    return;
                info[i].done = true;
                const QString name = QString::fromUtf16(
                    reinterpret_cast<const char16_t*>(er.names.constData() + er.nameOff[i]),
                    int(er.nameLen[i]) / 2);
                QString parentPath;
                bool parentEmit = true;
                if (er.frn[i] == 5) { // 卷根
                    parentPath = v.root;
                    parentEmit = false;
                } else if (er.parent[i] == 5) {
                    parentPath = v.root;
                } else {
                    const int p = byFrn.value(er.parent[i], -1);
                    if (p >= 0 && p != i) {
                        compose(p);
                        parentPath = QString::fromUtf8(v.arena.constData() + info[p].off, info[p].len);
                        parentEmit = info[p].emitFlag;
                    } else {
                        parentPath = v.root; // 孤儿记录挂根
                    }
                }
                const QString full = parentPath.endsWith('/')
                    ? parentPath + name : parentPath + "/" + name;
                const QByteArray utf8 = full.toUtf8();
                info[i].off = v.arena.size();
                v.arena.append(utf8);
                info[i].len = utf8.size();
                info[i].nameLen8 = int(name.toUtf8().size());
                info[i].emitFlag = parentEmit && er.frn[i] != 5 && name != "."
                                   && !name.startsWith('$') && !shouldSkip(name);
            };

            for (int i = 0; i < n; ++i) {
                compose(i);
                if (!info[i].emitFlag)
                    continue;
                FileDeepEntry e;
                e.offset = quint32(info[i].off);
                e.len = quint32(info[i].len);
                e.nameStart = e.len - quint32(info[i].nameLen8);
                e.isDir = (er.attrs[i] & FILE_ATTRIBUTE_DIRECTORY) != 0 ? 1 : 0;
                e.frn = er.frn[i];
                v.entries.append(e);
            }
            v.journalId = er.journal.journalId;
            v.nextUsn = er.journal.nextUsn;
        } else {
            // 无管理员权限 / 非 NTFS：目录遍历回退
            walkDeepVolume(root, &v.entries, &v.arena);
        }
        v.arena.squeeze();
        v.entries.squeeze();
        fresh.append(v);
    }

    // 写文件（同一 LTDI2 布局），随后映射文件并释放内存副本
    QSaveFile f(Utils::dataDir() + "/deepindex.dat");
    if (!f.open(QIODevice::WriteOnly)) {
        Utils::log("deep index: cannot write snapshot file");
        return;
    }
    QByteArray out;
    out.append("LTDI2", 5);
    const auto put32 = [&out](quint32 x) { char b[4]; memcpy(b, &x, 4); out.append(b, 4); };
    const auto put64 = [&out](quint64 x) { char b[8]; memcpy(b, &x, 8); out.append(b, 8); };
    put32(quint32(fresh.size()));
    for (const FreshVol& v : fresh) {
        const QByteArray rootUtf8 = v.root.toUtf8();
        put32(quint32(rootUtf8.size()));
        out.append(rootUtf8);
        put64(v.journalId);
        put64(v.nextUsn);
        put32(quint32(v.entries.size()));
        put32(quint32(v.arena.size()));
        QByteArray block;
        block.reserve(v.entries.size() * kPackedEntrySize);
        for (const FileDeepEntry& e : v.entries) {
            char b[kPackedEntrySize];
            memcpy(b, &e.offset, 4);
            memcpy(b + 4, &e.len, 4);
            memcpy(b + 8, &e.nameStart, 4);
            b[12] = e.isDir;
            memcpy(b + 13, &e.frn, 8);
            block.append(b, kPackedEntrySize);
        }
        out.append(block);
        out.append(v.arena);
    }
    f.write(out);
    f.commit();

    // 重映射文件并重置增量
    unmapSnapshot();
    if (!mapSnapshot()) {
        // 映射失败（极少）：从内存副本回退为普通结构不可行，直接标记无快照
        Utils::log("deep index: snapshot written but remap failed");
    }
    saveUsnState();
}

// ---------- 深索引：USN 增量 ----------

// 用 OpenFileById 拿一个文件引用号对应的完整路径（用于组合新增文件的路径）
QString parentPathById(HANDLE vol, quint64 parentFrn)
{
    FILE_ID_DESCRIPTOR fid;
    fid.dwSize = sizeof(fid);
    fid.Type = FileIdType;
    fid.FileId.QuadPart = LONGLONG(parentFrn);
    HANDLE ph = OpenFileById(vol, &fid, GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, 0);
    if (ph == INVALID_HANDLE_VALUE || !ph)
        return {};
    WCHAR buf[4096];
    const DWORD n = GetFinalPathNameByHandleW(ph, buf, 4096, FILE_NAME_NORMALIZED);
    CloseHandle(ph);
    if (n == 0)
        return {};
    QString path = QString::fromWCharArray(buf, int(n));
    path.remove("\\\\?\\");
    return QDir::fromNativeSeparators(path);
}

FileIndexer::DeepStatus FileIndexer::incrementalDeep()
{
    if (m_base.isEmpty())
        return DeepStatus::NeedRebuild;
    DeepStatus status = DeepStatus::Ok;
    int changed = 0;

    for (const MappedVol& mv : m_base) {
        // 找游标
        VolRuntime* cur = nullptr;
        for (VolRuntime& v : m_vols)
            if (v.root.compare(mv.root, Qt::CaseInsensitive) == 0)
                cur = &v;
        if (!cur) {
            m_vols.append({mv.root, mv.journalId, mv.nextUsn});
            cur = &m_vols.last();
        }
        if (cur->journalId == 0 || cur->nextUsn == 0 || mv.count == 0) {
            status = DeepStatus::NeedRebuild;
            continue;
        }
        UsnIndex::VolumeHandle vol;
        if (!vol.open(mv.root)) {
            Utils::log(QString("usn incremental skip %1: open failed err=%2 (需要管理员权限)")
                           .arg(mv.root).arg(GetLastError()));
            status = DeepStatus::NoAccess;
            continue;
        }
        const UsnIndex::JournalInfo j = UsnIndex::queryJournal(vol.vol);
        if (!j.ok || j.journalId != cur->journalId) {
            Utils::log(QString("usn incremental skip %1: journal mismatch").arg(mv.root));
            status = DeepStatus::NeedRebuild;
            continue;
        }
        QVector<UsnIndex::RawChange> changes;
        if (!UsnIndex::readChanges(vol.vol, cur->journalId, cur->nextUsn, j.nextUsn, changes)) {
            status = DeepStatus::NeedRebuild;
            continue;
        }
        if (changes.isEmpty()) {
            cur->nextUsn = j.nextUsn;
            continue;
        }

        const HANDLE volHandle = vol.vol;
        for (const UsnIndex::RawChange& c : changes) {
            const bool isDir = (c.attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (c.reason & (USN_REASON_FILE_CREATE | USN_REASON_RENAME_NEW_NAME)) {
                if (isDir && (c.reason & USN_REASON_RENAME_NEW_NAME))
                    return DeepStatus::NeedRebuild; // 目录改名 → 整棵子树路径失效
                if (shouldSkip(c.name) || c.name.startsWith('$') || c.name == ".")
                    continue;
                // 父目录路径：优先用 OpenFileById（不依赖任何内存索引）
                QString parentPath;
                if (c.parentFrn == 5) {
                    parentPath = mv.root;
                } else {
                    parentPath = parentPathById(volHandle, c.parentFrn);
                    if (parentPath.isEmpty()) {
                        // 父目录已不在（瞬间删除等）
                        continue;
                    }
                }
                if (!pathIndexable(parentPath))
                    continue;
                const QString full = parentPath.endsWith('/')
                    ? parentPath + c.name : parentPath + "/" + c.name;
                FileDeepEntry e;
                const QByteArray u8 = full.toUtf8();
                e.offset = quint32(m_addArena.size());
                m_addArena.append(u8);
                e.len = quint32(m_addArena.size() - e.offset);
                e.nameStart = e.len - quint32(c.name.toUtf8().size());
                e.isDir = isDir ? 1 : 0;
                e.frn = c.frn;
                m_add.append(e);
                ++changed;
            }
            if (c.reason & (USN_REASON_FILE_DELETE | USN_REASON_RENAME_OLD_NAME)) {
                // 删除 = 墓碑（基础快照按 frn 跳过）；增量新增的条目直接移除
                for (int i = m_add.size() - 1; i >= 0; --i) {
                    if (m_add[i].frn == c.frn) {
                        m_add.removeAt(i);
                        break;
                    }
                }
                m_removed.insert(c.frn);
                ++changed;
            }
        }
        cur->nextUsn = j.nextUsn;
        Q_UNUSED(changed);
    }
    saveUsnState();
    if (m_add.size() + m_removed.size() > 30000) {
        Utils::log("delta too large, triggering full rebuild");
        return DeepStatus::NeedRebuild;
    }
    return status;
}

bool FileIndexer::cacheTooOld() const
{
    QFileInfo f(Utils::dataDir() + "/deepindex.dat");
    if (!f.exists())
        return true;
    QSettings cfg(Utils::dataDir() + "/config.ini", QSettings::IniFormat);
    const int hours = cfg.value("index/refreshHours", 6).toInt();
    if (hours <= 0)
        return false;
    return f.lastModified().msecsTo(QDateTime::currentDateTime()) > qint64(hours) * 3600 * 1000;
}

void FileIndexer::removeDeltaByFrn(quint64 frn)
{
    for (int i = m_add.size() - 1; i >= 0; --i) {
        if (m_add[i].frn == frn) {
            m_add.removeAt(i);
            return;
        }
    }
}

void FileIndexer::applyUsnDelete(quint64 frn)
{
    m_removed.insert(frn);
    removeDeltaByFrn(frn);
}

// ---------- 深索引搜索 ----------

QVector<FileEntry> FileIndexer::search(const QString& query, int limit) const
{
    QVector<FileEntry> res;
    const QString q = query.trimmed();
    if (q.isEmpty())
        return res;

    struct Hit { FileEntry e; int score; int source; }; // source 0=快 1=深
    QVector<Hit> hits;

    {
        QReadLocker lock(&m_lock);

        for (const FileEntry& e : m_entries) {
            const int idx = e.name.indexOf(q, 0, Qt::CaseInsensitive);
            if (idx < 0)
                continue;
            const int score = (idx == 0 ? 0 : 2) + (e.isDir ? 0 : 1);
            hits.append({e, score, 0});
        }

        const QByteArray q8 = q.toLower().toUtf8();
        const bool hasTomb = !m_removed.isEmpty();
        // 映射快照
        for (const MappedVol& v : m_base) {
            const char* entries = (const char*)m_mapView + v.entryOff;
            const char* arena = (const char*)m_mapView + v.arenaOff;
            for (quint32 i = 0; i < v.count; ++i) {
                PackedDeepEntry de;
                memcpy(&de, entries + qint64(i) * kPackedEntrySize, kPackedEntrySize);
                if (hasTomb && m_removed.contains(de.frn))
                    continue;
                const char* nameStart = arena + de.offset + de.nameStart;
                const int nameLen = int(de.len) - int(de.nameStart);
                const int idx = indexOfCI(nameStart, nameLen, q8.constData(), int(q8.size()));
                if (idx < 0)
                    continue;
                const int score = (idx == 0 ? 0 : 2) + (de.isDir ? 0 : 1);
                const QString path = QString::fromUtf8(arena + de.offset, int(de.len));
                hits.append({{path.mid(int(de.nameStart)), path, de.isDir != 0}, score, 1});
            }
        }
        // 增量新增
        const char* addArena = m_addArena.constData();
        for (const FileDeepEntry& de : m_add) {
            const char* nameStart = addArena + de.offset + de.nameStart;
            const int nameLen = int(de.len) - int(de.nameStart);
            const int idx = indexOfCI(nameStart, nameLen, q8.constData(), int(q8.size()));
            if (idx < 0)
                continue;
            const int score = (idx == 0 ? 0 : 2) + (de.isDir ? 0 : 1);
            const QString path = QString::fromUtf8(addArena + de.offset, int(de.len));
            hits.append({{path.mid(int(de.nameStart)), path, de.isDir != 0}, score, 1});
        }
    }

    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        if (a.source != b.source)
            return a.source < b.source; // 快索引（更新鲜）优先
        if (a.score != b.score)
            return a.score < b.score;
        if (a.e.name.size() != b.e.name.size())
            return a.e.name.size() < b.e.name.size();
        return a.e.path < b.e.path;
    });

    // 按路径去重（快索引目录包含于全盘扫描范围）
    QSet<QString> seen;
    for (const Hit& h : hits) {
        const QString key = h.e.path.toLower();
        if (seen.contains(key))
            continue;
        seen.insert(key);
        res.append(h.e);
        if (res.size() >= limit)
            break;
    }
    return res;
}

// ---------- 快索引实时监听 ----------

void FileIndexer::startWatchers()
{
    if (m_watchCtx)
        return;
    auto* ctx = new WatchCtx;
    ctx->stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    for (const QuickRoot& qr : quickRoots()) {
        // 用户主目录子树过大且与其他目录重叠，不监听（其顶层列表靠快索引快照）
        if (qr.dir == home)
            continue;
        WatchRoot r;
        r.root = QDir::toNativeSeparators(qr.dir);
        r.dir = CreateFileW((LPCWSTR)r.root.utf16(), FILE_LIST_DIRECTORY,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING,
                            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (r.dir == INVALID_HANDLE_VALUE) {
            r.dir = nullptr;
            continue;
        }
        r.event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        r.buffer.resize(int(kQuickWatchBuffer));
        if (!issueWatch(r))
            continue;
        ctx->roots.append(r);
    }
    if (ctx->roots.isEmpty()) {
        CloseHandle(ctx->stopEvent);
        delete ctx;
        return;
    }
    m_watchCtx = ctx;
    std::thread([this] { watcherLoop(m_watchCtx); }).detach();
}

void FileIndexer::watcherLoop(void* ctxPtr)
{
    auto* ctx = static_cast<WatchCtx*>(ctxPtr);
    QVector<HANDLE> handles;
    handles.append(ctx->stopEvent);
    for (const WatchRoot& r : ctx->roots)
        handles.append(r.event);

    for (;;) {
        const DWORD wait = WaitForMultipleObjects(DWORD(handles.size()), handles.data(),
                                                  FALSE, INFINITE);
        if (wait == WAIT_OBJECT_0)
            return; // 退出
        if (wait < WAIT_OBJECT_0 + 1 || wait > WAIT_OBJECT_0 + DWORD(ctx->roots.size()))
            continue;
        WatchRoot& r = ctx->roots[int(wait - WAIT_OBJECT_0 - 1)];

        DWORD bytes = 0;
        bool overflow = false;
        if (!GetOverlappedResult(r.dir, &r.ov, &bytes, FALSE)) {
            if (GetLastError() == ERROR_NOTIFY_ENUM_DIR)
                overflow = true;
        } else if (bytes == 0) {
            overflow = true; // 缓冲区溢出
        }

        if (overflow) {
            rebuildQuickSync(); // 变化太多来不及逐条，整表重建
        } else {
            const char* base = r.buffer.constData();
            DWORD off = 0;
            for (;;) {
                if (off + sizeof(FILE_NOTIFY_INFORMATION) > bytes)
                    break;
                const FILE_NOTIFY_INFORMATION* fni =
                    reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(base + off);
                const QString name = QString::fromWCharArray(fni->FileName,
                                                             int(fni->FileNameLength / sizeof(WCHAR)));
                const QString full = QDir::fromNativeSeparators(r.root + "\\" + name);
                switch (fni->Action) {
                case FILE_ACTION_ADDED:
                case FILE_ACTION_RENAMED_NEW_NAME:
                    watcherApplyAdd(full);
                    break;
                case FILE_ACTION_REMOVED:
                case FILE_ACTION_RENAMED_OLD_NAME:
                    watcherApplyRemove(full);
                    break;
                default:
                    break; // MODIFIED 等与文件名索引无关
                }
                if (fni->NextEntryOffset == 0)
                    break;
                off += fni->NextEntryOffset;
            }
        }
        issueWatch(r);
    }
}

void FileIndexer::rebuildQuickSync()
{
    QVector<FileEntry> quick;
    quick.reserve(8192);
    for (const QuickRoot& r : quickRoots())
        walk(r.dir, r.depth, &quick, 200000);
    QWriteLocker lock(&m_lock);
    m_entries = quick;
}

void FileIndexer::watcherApplyAdd(const QString& path)
{
    const QFileInfo fi(path);
    if (!fi.exists())
        return;
    const QString fn = fi.fileName();
    if (shouldSkip(fn))
        return;
    QWriteLocker lock(&m_lock);
    // 重叠监听（桌面在用户目录子树内）会产生重复事件，按路径去重
    for (const FileEntry& e : m_entries)
        if (QString::compare(e.path, path, Qt::CaseInsensitive) == 0)
            return;
    m_entries.append({fn, path, fi.isDir()});
}

void FileIndexer::watcherApplyRemove(const QString& path)
{
    const QString prefix = path + '/';
    QWriteLocker lock(&m_lock);
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
                                   [&](const FileEntry& e) {
                                       return QString::compare(e.path, path, Qt::CaseInsensitive) == 0
                                              || e.path.startsWith(prefix, Qt::CaseInsensitive);
                                   }),
                    m_entries.end());
}
