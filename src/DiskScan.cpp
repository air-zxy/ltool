#include "DiskScan.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QHash>

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>

namespace {

constexpr int kMaxDepth = 64;
constexpr int kSplitKids = 12;      // 直接子目录多于这个数的目录拆成独立任务
constexpr int kMaxJobs = 4000;      // 拆分预算，免得把小目录也拆出一堆任务

// 一个任务扫出来的片段。chain = 该目录相对卷根的祖先链（不含自己）；
// name/direct/parent 是片段内节点，parent=-1 只出现在 0 号节点（=任务目录本身）。
struct Unit {
    QStringList chain;
    QVector<QString> name;
    QVector<qint64> direct;
    QVector<int> parent;
};

struct Counters {
    std::atomic<qint64> bytes{0};
    std::atomic<qint64> dirs{0};
    std::atomic<qint64> files{0};
    std::mutex m;
    QString current;    // 正在扫的分支，给界面当"还活着"的证据

    // 一级分支的完成度（用途见 DiskScan.h 里 Progress.doneBranches 的说明）。
    // pending[top] = 这个分支名下还有多少个任务在飞。先给孩子 +1、最后才把自己 -1，
    // 所以只有"孩子也都交完了"才会判完成 —— 反过来做就会把一个还在扫的分支报成定稿。
    // 用定长数组而不是 vector<atomic<>>：atomic 既不可复制也不可移动，
    // vector 的 reserve/resize 要求 MoveConstructible，MSVC 直接在 xmemory 里报 C2672。
    static constexpr int kMaxBranches = 512;   // 卷根下面一般只有几十个子目录
    QStringList branchNames;
    std::array<std::atomic<int>, kMaxBranches> pending{};
    std::array<std::atomic<qint64>, kMaxBranches> acc{};
    int branchCount = 0;
    std::mutex bm;
    QVector<QPair<QString, qint64>> done;

    void setupBranches(const QStringList& tops)
    {
        branchNames = tops;
        branchCount = qMin(int(tops.size()), kMaxBranches);
        for (int i = 0; i < branchCount; ++i) {
            pending[i].store(1, std::memory_order_relaxed);   // 每个一级分支自己就是一个任务
            acc[i].store(0, std::memory_order_relaxed);
        }
    }
    void addBranchJob(int top)
    {
        if (top >= 0 && top < branchCount)
            pending[top].fetch_add(1, std::memory_order_relaxed);
    }
    void finishJob(int top, qint64 subtreeBytes)
    {
        if (top < 0 || top >= branchCount)
            return;
        acc[top].fetch_add(subtreeBytes, std::memory_order_relaxed);
        if (pending[top].fetch_sub(1, std::memory_order_acq_rel) != 1)
            return;                       // 还有孩子在飞，这个分支的数还没定
        const qint64 bytes = acc[top].load(std::memory_order_relaxed);
        const QString name = branchNames[top];
        const std::lock_guard<std::mutex> g(bm);
        done.append({ name, bytes });
    }
};

// 列一层目录：返回直属文件字节，顺带把子目录名收进 kids。
// 用 FindFirstFileExW(FindExInfoBasic) + LARGE_FETCH —— 目录项自带大小和属性，
// 省掉每个文件一次 stat，这是 QDirIterator + QFileInfo::size() 慢的主要原因。
//
// **文件名只在真的是目录时才转 QString**：这台机器一趟要过 142 万个文件，
// 以前每个都 fromWCharArray 一次再丢掉（`QString` 一次两回堆分配），
// 而文件我们只要 nFileSizeHigh/Low。改完这一处再拿 --diskscale 量差值。
qint64 listDir(const QString& path, QStringList& kids, std::atomic<qint64>* files = nullptr)
{
    qint64 direct = 0;
    qint64 localFiles = 0;
    WIN32_FIND_DATAW fd;
    const QString pattern = path + QStringLiteral("/*");
    HANDLE h = FindFirstFileExW((LPCWSTR)pattern.utf16(), FindExInfoBasic, &fd,
                                FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        const DWORD attr = fd.dwFileAttributes;
        if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            // 普通文件：只加字节，连名字都不解（属性读失败的怪项也归这里，按"不是目录"处理）
            direct += qint64((quint64(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow);
            ++localFiles;
            continue;
        }
        if (attr & FILE_ATTRIBUTE_REPARSE_POINT)
            continue;                       // junction / 目录软链：跳过，否则重复计数甚至死循环
        const wchar_t* n = fd.cFileName;
        if (n[0] == L'.' && (!n[1] || (n[1] == L'.' && !n[2])))
            continue;                       // "." 和 ".."
        kids.append(QString::fromWCharArray(n));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (files)
        files->fetch_add(localFiles, std::memory_order_relaxed);
    return direct;
}

// 把 path 的整棵子树收进片段 u（self 是 path 在 u 里的下标）。
// 这里只累加"直属字节"；子树合计在合并阶段自下而上算一遍，比递归回传省事。
void collect(Unit& u, int self, const QString& path, int depth, Counters& c)
{
    QStringList kids;
    const qint64 direct = listDir(path, kids, &c.files);
    u.direct[self] += direct;
    c.bytes.fetch_add(direct, std::memory_order_relaxed);
    c.dirs.fetch_add(1, std::memory_order_relaxed);
    if (depth <= 0)
        return;
    for (const QString& k : kids) {
        const int idx = u.name.size();
        u.name.append(k);
        u.direct.append(0);
        u.parent.append(self);
        collect(u, idx, path + QLatin1Char('/') + k, depth - 1, c);
    }
}

struct Job {
    QStringList chain;
    QString name;
    QString path;
    int top = -1;         // 属于哪个一级分支（拆出去的子任务继承父任务的值）
};

struct Queue {
    std::deque<Job> items;
    std::mutex m;
    std::atomic<int> pushed{0};
};

// 扫一个任务目录。子目录太多时把它们塞回队列，让先干完的线程接手 ——
// 之前按顶层目录轮转分配，C:\Users 一个目录占全盘七成，等于一个线程扛全部，
// 其余线程早早收工，界面就一直停在"扫描中…"。
void runJob(const Job& job, Unit& u, Queue& q, Counters& c)
{
    QStringList kids;
    const qint64 direct = listDir(job.path, kids, &c.files);
    {
        const std::lock_guard<std::mutex> g(c.m);
        c.current = (job.chain.isEmpty()
                         ? QString()
                         : job.chain.join(QLatin1Char('\\')) + QLatin1Char('\\'))
                    + job.name;
    }
    u.chain = job.chain;
    u.name.append(job.name);
    u.direct.append(direct);
    u.parent.append(-1);
    c.bytes.fetch_add(direct, std::memory_order_relaxed);
    c.dirs.fetch_add(1, std::memory_order_relaxed);

    if (kids.size() >= kSplitKids
        && q.pushed.load(std::memory_order_relaxed) < kMaxJobs) {
        QStringList chain = job.chain;
        chain.append(job.name);
        int i = 0;
        {
            const std::lock_guard<std::mutex> g(q.m);
            for (; i < kids.size(); ++i) {
                if (q.pushed.fetch_add(1, std::memory_order_relaxed) >= kMaxJobs) {
                    q.pushed.fetch_sub(1, std::memory_order_relaxed);
                    break;
                }
                // 先把这个分支的"在飞任务数"加一，再入队 —— 反过来做的话，
                // 本任务交账时 pending 可能已经归零，把还在扫的分支报成定稿。
                c.addBranchJob(job.top);
                q.items.push_front(Job{chain, kids[i], job.path + QLatin1Char('/') + kids[i],
                                       job.top});
            }
        }
        // 预算用完剩下的孩子必须自己扫掉。之前这里是直接 break —— 那些目录既没入队
        // 也没被扫，实测整整丢了十几个 G（Windows 从 40.9 GB 变 24 GB）。
        for (; i < kids.size(); ++i) {
            const int idx = u.name.size();
            u.name.append(kids[i]);
            u.direct.append(0);
            u.parent.append(0);
            collect(u, idx, job.path + QLatin1Char('/') + kids[i], kMaxDepth - 1, c);
        }
        return;
    }
    for (const QString& k : kids) {
        const int idx = u.name.size();
        u.name.append(k);
        u.direct.append(0);
        u.parent.append(0);
        collect(u, idx, job.path + QLatin1Char('/') + k, kMaxDepth - 1, c);
    }
}

} // namespace

namespace DiskScan {

QString Tree::pathOf(int node) const
{
    if (node == root)
        return QDir::toNativeSeparators(rootPath);
    QStringList parts;
    for (int i = node; i >= 0 && i != root; i = parent[i])
        parts.prepend(name[i]);
    return QDir::toNativeSeparators(rootPath + QLatin1Char('/') + parts.join(QLatin1Char('/')));
}

int Tree::child(int node, const QString& target) const
{
    for (int i = firstKid[node]; i < firstKid[node] + kidCount[node]; ++i)
        if (kids[i] >= 0 && name[kids[i]].compare(target, Qt::CaseInsensitive) == 0)
            return kids[i];
    return -1;
}

int Tree::nodeForPath(const QString& absPath) const
{
    if (absPath.isEmpty() || isEmpty())
        return -1;
    QString p = QDir::toNativeSeparators(QDir::cleanPath(absPath));
    const QString base = QDir::toNativeSeparators(rootPath);
    if (!p.startsWith(base, Qt::CaseInsensitive))
        return -1;
    QString rest = p.mid(base.length());
    const QChar sep(92);   // 反斜杠，写成 QChar 免得源码里到处转义
    while (rest.startsWith(sep))
        rest.remove(0, 1);
    int node = root;
    if (rest.isEmpty())
        return node;
    for (const QString& seg : rest.split(sep, Qt::SkipEmptyParts)) {
        node = child(node, seg);
        if (node < 0)
            return -1;
    }
    return node;
}

namespace {
DiskScan::Timing g_timing;
} // namespace

DiskScan::Timing DiskScan::lastTiming()
{
    return g_timing;
}

Tree run(const QString& rootPath, qint64 minBytes,
         const std::function<void(const Progress&)>& progress)
{
    Tree t;
    QElapsedTimer et;
    et.start();
    g_timing = DiskScan::Timing();
    const QString base = QDir(rootPath).absolutePath();   // 形如 C:/
    t.rootPath = base;

    QDir d(base);
    if (!d.exists())
        return t;

    Queue q;
    Counters c;
    QStringList tops;
    const qint64 rootDirect = listDir(base, tops);
    c.setupBranches(tops);          // 先建好计数器，再往里塞任务（反了会漏加 pending）
    for (int i = 0; i < tops.size(); ++i) {
        q.items.push_back(Job{ QStringList{}, tops[i], base + QLatin1Char('/') + tops[i], i });
        q.pushed.fetch_add(1, std::memory_order_relaxed);
    }

    const int hw = int(std::thread::hardware_concurrency());
    int want = qBound(4, hw ? hw : 4, 8);
    if (const QByteArray forced = qgetenv("LTOOL_DISKTHREADS"); !forced.isEmpty())
        want = qBound(1, forced.toInt(), 64);   // 只给 --diskscale 量扩展性用
    const int threads = want;
    std::vector<std::vector<Unit>> out(threads);

    // 进度只由这个线程统一发：扫描线程只管更新原子量，不回调界面代码
    std::atomic<bool> stop{false};
    std::thread reporter;
    if (progress) {
        reporter = std::thread([&] {
            while (!stop.load(std::memory_order_acquire)) {
                Sleep(200);
                Progress p;
                p.bytes = c.bytes.load(std::memory_order_relaxed);
                p.dirs = c.dirs.load(std::memory_order_relaxed);
                p.branches = c.branchCount;
                {
                    const std::lock_guard<std::mutex> g(c.m);
                    p.current = c.current;
                }
                {
                    const std::lock_guard<std::mutex> g(c.bm);
                    p.doneBranches = c.done;
                }
                progress(p);
            }
        });
    }

    std::vector<std::thread> pool;
    pool.reserve(threads);
    // active = 正在被扫描、还没交回结果的任务数。少了它就会出现"队列瞬时为空 →
    // 线程提前收工，而另一个线程正准备往队列里塞子目录"，实测会整整漏掉十几个 GB
    // （Windows 目录从 40.9 GB 变成 24 GB），必须等"队列空且没有在飞的任务"才退出。
    std::atomic<int> active{0};
    for (int k = 0; k < threads; ++k) {
        pool.emplace_back([&, k] {
            for (;;) {
                Job job;
                {
                    const std::lock_guard<std::mutex> g(q.m);
                    if (!q.items.empty()) {
                        job = q.items.back();
                        q.items.pop_back();
                        active.fetch_add(1, std::memory_order_relaxed);
                    } else if (active.load(std::memory_order_relaxed) == 0) {
                        return;
                    } else {
                        Sleep(1);   // 还有人在扫：让出锁和 CPU，别空转把塞任务的线程饿住
                        continue;
                    }
                }
                Unit u;
                runJob(job, u, q, c);
                // 这一份片段就是"这个任务名下没拆出去的部分"的全部字节；
                // 拆出去的孩子各自会交自己那份，所以 acc[top] 累到最后 = 整棵分支
                qint64 subtree = 0;
                for (const qint64 v : u.direct)
                    subtree += v;
                c.finishJob(job.top, subtree);
                active.fetch_sub(1, std::memory_order_relaxed);
                out[k].push_back(std::move(u));
            }
        });
    }
    for (auto& th : pool)
        th.join();
    stop.store(true, std::memory_order_release);
    if (reporter.joinable())
        reporter.join();

    g_timing.walkMs = et.elapsed();
    g_timing.files = c.files.load(std::memory_order_relaxed);
    g_timing.dirsSeen = c.dirs.load(std::memory_order_relaxed);
    et.restart();
    // ---- 合并：根节点 + 按路径链把各片段挂回树上 ----
    t.root = 0;
    t.name << QStringLiteral("\\");
    t.fileBytes << rootDirect;
    t.parent << -1;

    const QString baseKey = base.toLower();
    QHash<QString, int> byPath;   // 小写路径 → 全局下标；只有目录节点进这张表
    byPath.insert(baseKey, 0);
    const auto ensureChain = [&](const QStringList& chain) {
        int node = 0;
        QString key = baseKey;
        for (const QString& seg : chain) {
            key += QLatin1Char('/') + seg.toLower();
            auto it = byPath.constFind(key);
            if (it != byPath.constEnd()) {
                node = it.value();
                continue;
            }
            const int idx = t.name.size();
            t.name.append(seg);
            t.fileBytes.append(0);
            t.parent.append(node);
            byPath.insert(key, idx);
            node = idx;
        }
        return node;
    };

    for (const std::vector<Unit>& part : out) {
        for (const Unit& u : part) {
            if (u.name.isEmpty())
                continue;
            QStringList full = u.chain;
            full.append(u.name[0]);
            const int anchor = ensureChain(full);   // 任务目录本身（可能被别的单元的链先建出来）
            const int b = t.name.size();            // 片段其余节点的起点
            for (int i = 1; i < u.name.size(); ++i) {
                t.name.append(u.name[i]);
                t.fileBytes.append(u.direct[i]);
                t.parent.append(u.parent[i] <= 0 ? anchor : b + u.parent[i] - 1);
            }
            t.fileBytes[anchor] += u.direct[0];
        }
    }

    g_timing.mergeMs = et.elapsed();
    et.restart();
    // ---- 子树合计：父下标一定小于子下标，倒着一遍就够 ----
    const int n = t.name.size();
    t.size = t.fileBytes;
    for (int i = n - 1; i > 0; --i)
        t.size[t.parent[i]] += t.size[i];
    t.total = t.size[0];

    // ---- 建孩子表：先数、再前缀和、再填 ----
    QVector<int> count(n, 0);
    for (int i = 1; i < n; ++i)
        count[t.parent[i]]++;
    t.firstKid.resize(n);
    t.kidCount.resize(n);
    t.kids.resize(qMax(1, n - 1));
    int acc = 0;
    for (int i = 0; i < n; ++i) {
        t.firstKid[i] = acc;
        t.kidCount[i] = count[i];
        acc += count[i];
    }
    QVector<int> cursor = t.firstKid;
    for (int i = 1; i < n; ++i)
        t.kids[cursor[t.parent[i]]++] = i;

    // ---- 每个父目录的孩子按大小降序；太小的不列出 ----
    g_timing.mergeMs += et.restart();   // 上面"自下而上求和 + 建孩子表"也算进合并这段
    t.pruned = 0;
    for (int p = 0; p < n; ++p) {
        const int from = t.firstKid[p];
        const int cnt = t.kidCount[p];
        if (cnt <= 0)
            continue;
        int* arr = t.kids.data() + from;
        const qint64* sizes = t.size.constData();
        std::sort(arr, arr + cnt, [sizes](int a, int b) { return sizes[a] > sizes[b]; });
        int keep = 0;
        while (keep < cnt && t.size[arr[keep]] >= minBytes)
            ++keep;
        if (p == 0)
            keep = cnt;   // 根层永远全留，否则用户看不到全貌
        t.pruned += cnt - keep;
        t.kidCount[p] = keep;
    }
    g_timing.sortMs = et.elapsed();
    g_timing.jobs = int(q.pushed.load(std::memory_order_relaxed));
    g_timing.threads = threads;
    return t;
}

QVector<CleanTarget> cleanTargets()
{
    const QString local = QDir::homePath() + "/AppData/Local";
    return {
        {"用户临时文件", QDir::tempPath(), false},
        {"系统临时文件", "C:/Windows/Temp", true},
        {"Windows 更新缓存", "C:/Windows/SoftwareDistribution/Download", true},
        {"DirectX 着色器缓存", local + "/D3DSCache", false},
        {"CrashDumps 崩溃转储", local + "/CrashDumps", false},
        {"pip 缓存", local + "/pip/cache", false},
        {"npm 缓存", local + "/npm-cache", false},
        {"Chrome 缓存", local + "/Google/Chrome/User Data/Default/Cache", false},
        {"Edge 缓存", local + "/Microsoft/Edge/User Data/Default/Cache", false},
    };
}

QVector<qint64> sizesFor(const QStringList& paths,
                         const std::function<void(const Progress&)>& progress)
{
    QVector<qint64> result(paths.size(), 0);
    if (paths.isEmpty())
        return result;
    Counters c;
    std::atomic<int> next{0};
    const int threads = qBound(1, int(paths.size()), 6);
    std::vector<std::thread> pool;
    pool.reserve(threads);
    for (int k = 0; k < threads; ++k) {
        pool.emplace_back([&] {
            for (;;) {
                const int i = next.fetch_add(1, std::memory_order_relaxed);
                if (i >= paths.size())
                    break;
                const QString p = QDir::cleanPath(paths[i]);
                Unit u;
                u.name.append(QFileInfo(p).fileName());
                u.direct.append(0);
                u.parent.append(-1);
                collect(u, 0, p, kMaxDepth, c);
                qint64 sum = 0;
                for (qint64 v : u.direct)
                    sum += v;
                result[i] = sum;
                if (progress) {
                    Progress pr;
                    pr.bytes = c.bytes.load(std::memory_order_relaxed);
                    pr.dirs = c.dirs.load(std::memory_order_relaxed);
                    pr.current = QDir::toNativeSeparators(p);
                    progress(pr);
                }
            }
        });
    }
    for (auto& th : pool)
        th.join();
    return result;
}

qint64 legacyWalk(const QString& rootPath)
{
    qint64 bytes = 0;
    // Hidden|System 必须带上：不带的话 pagefile/系统目录整个被跳过，
    // 基线数会少一半（实测 105 GB vs 209 GB），拿它做 A/B 就是自己骗自己
    QDirIterator it(rootPath, QDir::Files | QDir::NoSymLinks | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        bytes += it.fileInfo().size();
    }
    return bytes;
}

} // namespace DiskScan
