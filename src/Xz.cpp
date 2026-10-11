#include "Xz.h"

#include "Utils.h"

#include <lzma.h>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QThread>

#include <algorithm>
#include <vector>

namespace {

constexpr char kMagic[4] = { 'L', 'Z', 'X', '1' };
constexpr quint8 kVersion = 1;
constexpr int kInBuf = 1 << 16;
constexpr int kOutBuf = 1 << 20;
// Block 大小的上下限：多线程是按 Block 分活的，一个 2GB 的文件不切就只有一条线程在跑；
// 但 Block 太小会各自带一份字典，压缩率掉得多。中间取"每个线程约 2 个 Block"。
constexpr uint64_t kBlockMin = 1ULL * 1024 * 1024;
constexpr uint64_t kBlockMax = 16ULL * 1024 * 1024;

struct Job {
    QString path;      // 磁盘上的全路径
    QString name;      // 包里的名字（'/' 分隔，和 zip/7z 那条一致：目录输入会带上目录名）
    qint64 size = 0;
    qint64 offset = 0;   // 在拼接流里的起点（解出来之后按这个切）
};

QString lzErr(lzma_ret r)
{
    switch (r) {
    case LZMA_MEM_ERROR:         return QStringLiteral("内存不够");
    case LZMA_OPTIONS_ERROR:     return QStringLiteral("liblzma 不接受这组参数");
    case LZMA_UNSUPPORTED_CHECK: return QStringLiteral("这个 xz 流用了没开支持的校验");
    case LZMA_DATA_ERROR:        return QStringLiteral("数据坏了（包不完整或被改过）");
    case LZMA_BUF_ERROR:         return QStringLiteral("包比声明的短");
    case LZMA_PROG_ERROR:        return QStringLiteral("调用 liblzma 的方式不对");
    default:                     return QStringLiteral("liblzma 返回 %1").arg(int(r));
    }
}

// 挡一手"包里写着 ../x 就把文件拉到目标目录外面"
bool safeUnder(const QString& root, const QString& target)
{
    const QString r = QDir::toNativeSeparators(root).toLower();
    const QString t = QDir::toNativeSeparators(target).toLower();
    return t == r || t.startsWith(r + QLatin1Char('\\'));
}

// 目录树走一遍：文件按名字排序（包做得出来是确定的，自测才好比对），顺带记下空目录
struct Tree { QVector<Job> jobs; QStringList dirs; qint64 total = 0; };

Tree collect(const QStringList& inputs)
{
    Tree t;
    for (const QString& in0 : inputs) {
        // 一律先转成绝对路径再走目录：QDirIterator 给的是"用什么路径进来就用什么出去"，
        // 传相对路径会让条目名带上整个相对前缀（实测：src/f0.log 变成 build/verify/arspd/src/f0.log）
        const QString in = QFileInfo(in0).absoluteFilePath();
        const QFileInfo ri(in);
        if (!ri.exists())
            continue;
        const QString parent = ri.absolutePath();
        if (!ri.isDir()) {
            Job j;
            j.path = ri.absoluteFilePath();
            j.name = QDir(parent).relativeFilePath(j.path).replace(QLatin1Char('\\'), QLatin1Char('/'));
            j.size = ri.size();
            t.jobs.append(j);
            continue;
        }
        QDirIterator dit(in, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                         QDirIterator::Subdirectories);
        while (dit.hasNext()) {
            const QString d = dit.next();
            t.dirs << QDir(parent).relativeFilePath(d).replace(QLatin1Char('\\'), QLatin1Char('/'));
        }
        QDirIterator fit(in, QDir::Files | QDir::NoSymLinks | QDir::Hidden | QDir::System,
                         QDirIterator::Subdirectories);
        while (fit.hasNext()) {
            const QString f = fit.next();
            Job j;
            j.path = f;
            j.name = QDir(parent).relativeFilePath(f).replace(QLatin1Char('\\'), QLatin1Char('/'));
            j.size = QFileInfo(f).size();
            t.jobs.append(j);
        }
    }
    std::sort(t.jobs.begin(), t.jobs.end(),
              [](const Job& a, const Job& b) { return a.name < b.name; });
    t.dirs.sort();
    t.dirs.removeDuplicates();
    for (Job& j : t.jobs) {
        j.offset = t.total;
        t.total += j.size;
    }
    return t;
}

} // namespace

namespace Xz {

bool looksLike(const QString& path)
{
    if (!path.endsWith(QStringLiteral(".lzx"), Qt::CaseInsensitive))
        return false;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return f.read(4) == QByteArray(kMagic, 4);
}

Archive::Result compress(const QStringList& inputs, const QString& destFile, int threads,
                         const Archive::Report& report, const Archive::Cancel& cancel)
{
    Archive::Result res;
    const Tree tree = collect(inputs);
    if (tree.jobs.isEmpty()) {
        res.error = QStringLiteral("这里面没有文件");
        return res;
    }
    int th = threads;
    if (th <= 0)
        th = qMax(1, qMin(16, QThread::idealThreadCount()));

    QSaveFile out(destFile);
    if (!out.open(QIODevice::WriteOnly)) {
        res.error = QStringLiteral("建不出 %1（路径不可写？)").arg(destFile);
        return res;
    }

    // ---- 头部 + 清单 ----
    QJsonArray files;
    for (const Job& j : tree.jobs) {
        QJsonObject o;
        o.insert(QStringLiteral("n"), j.name);
        o.insert(QStringLiteral("s"), j.size);
        files.append(o);
    }
    QJsonObject rootObj;
    rootObj.insert(QStringLiteral("files"), files);
    rootObj.insert(QStringLiteral("dirs"), QJsonArray::fromStringList(tree.dirs));
    const QByteArray man = QJsonDocument(rootObj).toJson(QJsonDocument::Compact);
    QByteArray head(kMagic, 4);
    head.append(char(kVersion));
    const quint32 manLen = quint32(man.size());
    head.append(reinterpret_cast<const char*>(&manLen), 4);
    head.append(man);
    if (out.write(head) != head.size()) {
        res.error = QStringLiteral("写文件头失败");
        out.cancelWriting();
        return res;
    }

    // ---- 多线程 LZMA2 ----
    lzma_stream strm = LZMA_STREAM_INIT;
    lzma_mt opt {};
    opt.preset = 6;                       // 和 7-Zip 的 -mx=6 同一档
    opt.block_size = qint64(qBound<uint64_t>(kBlockMin,
                                             quint64(tree.total) / quint64(qMax(1, th * 2)),
                                             kBlockMax));
    opt.threads = quint32(th);
    const uint64_t need = lzma_stream_encoder_mt_memusage(&opt);
    lzma_ret r = lzma_stream_encoder_mt(&strm, &opt);
    if (r != LZMA_OK) {
        lzma_end(&strm);
        out.cancelWriting();
        res.error = QStringLiteral("开不了多线程编码器：%1").arg(lzErr(r));
        return res;
    }

    // 注意别写 std::vector<char> ob(size_t(kOutBuf))：那是"声明一个叫 ob、参数是
    // size_t kOutBuf 的函数"（most vexing parse），后面 .data() 全崩；用 resize 避开。
    std::vector<char> ob;
    ob.resize(size_t(kOutBuf));
    strm.next_out = reinterpret_cast<uint8_t*>(ob.data());
    strm.avail_out = ob.size();

    std::vector<char> ib;
    ib.resize(size_t(kInBuf));

    // 把已经压出来的字节倒进文件，然后继续喂。
    // 规则（liblzma 的 base.h 里写得很硬）：**一旦用了 FULL_FLUSH / FINISH 这类特殊动作，
    // 就必须一直用同一个动作喂到 lzma_code() 返回 LZMA_STREAM_END 为止**，中途换成别的
    // 就是 LZMA_PROG_ERROR(11)。LZMA_RUN 不一样，它是"把手头的输入吃完就回来"。
    auto pump = [&](lzma_action act) -> bool {
        for (;;) {
            const lzma_ret rr = lzma_code(&strm, act);
            const qint64 have = qint64(ob.size()) - qint64(strm.avail_out);
            if (have > 0) {
                if (out.write(ob.data(), int(have)) != have) {
                    res.error = QStringLiteral("写不出来（磁盘满了？）");
                    return false;
                }
                strm.next_out = reinterpret_cast<uint8_t*>(ob.data());
                strm.avail_out = ob.size();
            }
            if (rr == LZMA_STREAM_END)
                return true;
            if (rr != LZMA_OK) {
                res.error = QStringLiteral("%1（liblzma 返回 %2，动作 %3）")
                                .arg(lzErr(rr)).arg(int(rr)).arg(int(act));
                return false;
            }
            if (act == LZMA_RUN && strm.avail_in == 0)
                return true;
        }
    };

    qint64 done = 0;
    bool stopped = false;
    for (const Job& j : tree.jobs) {
        QFile f(j.path);
        if (!f.open(QIODevice::ReadOnly)) {
            res.error = QStringLiteral("读不了：%1").arg(j.path);
            stopped = true;
            break;
        }
        for (;;) {
            const qint64 got = f.read(ib.data(), qint64(ib.size()));
            if (got <= 0)
                break;
            strm.next_in = reinterpret_cast<const uint8_t*>(ib.data());
            strm.avail_in = size_t(got);
            while (strm.avail_in > 0) {
                if (!pump(LZMA_RUN)) {
                    stopped = true;
                    break;
                }
            }
            if (stopped)
                break;
            done += got;
            if (report && !report(Archive::Progress{ done, tree.total, j.name })) {
                res.cancelled = true;      // 界面那边只有按了取消才会返回 false
                stopped = true;
                break;
            }
            if (cancel && cancel()) {
                res.cancelled = true;
                stopped = true;
                break;
            }
        }
        f.close();
        if (stopped)
            break;
        // 这里**不**按文件 FULL_FLUSH：FULL_FLUSH 是个"等所有线程交工"的屏障，
        // 十几个小文件一人一个屏障就直接退化成串行了（实测 26MB / 13 个文件从 0.9 秒变 3.4 秒）。
        // 解包那边本来也是整条流一次解完再按清单切，不需要块和文件对齐。
        ++res.files;
        res.dirs = tree.dirs.size();
    }
    if (!stopped && !res.cancelled && !pump(LZMA_FINISH))
        stopped = true;
    lzma_end(&strm);
    res.bytes = done;
    if (res.cancelled) {
        out.cancelWriting();
        res.error = QStringLiteral("已取消，半成品已删掉");
        return res;
    }
    if (stopped) {
        out.cancelWriting();
        return res;
    }
    if (!out.commit()) {
        res.error = QStringLiteral("%1 没写成（磁盘满，或者被别的程序占着）").arg(destFile);
        return res;
    }
    Utils::log(QStringLiteral("lzx: 压完 %1 个文件 / %2 字节 → %3，线程=%4，编码器要 %5 MB")
                   .arg(res.files).arg(done).arg(QDir::toNativeSeparators(destFile)).arg(th)
                   .arg(need == UINT64_MAX ? 0 : int(need / 1048576)));
    res.ok = true;
    res.target = QDir::toNativeSeparators(destFile);
    return res;
}

Archive::Result extract(const QString& archivePath, const QString& destDir,
                        const Archive::Report& report, const Archive::Cancel& cancel)
{
    Archive::Result res;
    QFile f(archivePath);
    if (!f.open(QIODevice::ReadOnly)) {
        res.error = QStringLiteral("打不开 %1").arg(archivePath);
        return res;
    }
    // Qt 6.10 把 QIODevice::getChar() 换成了 bool getChar(char*)，一次读够头 5 字节更省事
    const QByteArray headIn = f.read(5);
    if (headIn.size() < 5 || headIn.left(4) != QByteArray(kMagic, 4)
        || quint8(headIn.at(4)) != kVersion) {
        res.error = QStringLiteral("不是我能认的 .lzx（版本不对）");
        return res;
    }
    quint32 manLen = 0;
    if (f.read(reinterpret_cast<char*>(&manLen), 4) != 4 || manLen == 0 || manLen > 16u * 1024 * 1024) {
        res.error = QStringLiteral(".lzx 的清单长度不对，这个包坏了");
        return res;
    }
    const QJsonObject rootObj = QJsonDocument::fromJson(f.read(int(manLen))).object();
    const QJsonArray files = rootObj.value(QStringLiteral("files")).toArray();
    qint64 total = 0;
    for (const QJsonValue& v : files)
        total += qint64(v.toObject().value(QStringLiteral("s")).toDouble());
    if (total > kExtractCap) {
        res.error = QStringLiteral("这个 .lzx 解出来有 %1 MB，超过 %2 MB 的上限 —— "
                                  ".lzx 解包要一次把整包还原进内存，这么大的东西请用 .zip 或 .7z")
                        .arg(total / 1048576).arg(kExtractCap / 1048576);
        return res;
    }
    const QByteArray stream = f.readAll();
    f.close();

    QByteArray data(int(total), '\0');
    size_t inPos = 0, outPos = 0;
    uint64_t memLimit = UINT64_MAX;
    const lzma_ret r = lzma_stream_buffer_decode(
        &memLimit, 0, nullptr,
        reinterpret_cast<const uint8_t*>(stream.constData()), &inPos, size_t(stream.size()),
        reinterpret_cast<uint8_t*>(data.data()), &outPos, size_t(total));
    if (r != LZMA_OK || outPos != size_t(total)) {
        res.error = r == LZMA_OK ? QStringLiteral("解出来的字节数不对（包不完整）")
                                 : QStringLiteral("解 .lzx 失败：%1").arg(lzErr(r));
        return res;
    }

    const QString nativeDest = QDir::toNativeSeparators(destDir);
    QDir().mkpath(nativeDest);
    for (const QJsonValue& dv : rootObj.value(QStringLiteral("dirs")).toArray()) {
        const QString p = QDir::cleanPath(nativeDest + QLatin1Char('/') + dv.toString());
        if (safeUnder(nativeDest, p))
            QDir().mkpath(p);
    }
    qint64 off = 0;
    for (const QJsonValue& v : files) {
        const QJsonObject o = v.toObject();
        const QString name = o.value(QStringLiteral("n")).toString();
        const qint64 size = qint64(o.value(QStringLiteral("s")).toDouble());
        const QString outPath = QDir::cleanPath(nativeDest + QLatin1Char('/') + name);
        if (!safeUnder(nativeDest, outPath)) {
            off += size;
            continue;                       // 想写到外面去的条目一律不写
        }
        QDir().mkpath(QFileInfo(outPath).absolutePath());
        QFile w(outPath);
        if (!w.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            res.error = QStringLiteral("写不出来：%1").arg(outPath);
            return res;
        }
        qint64 written = 0;
        while (written < size) {
            // 必须夹到"这个条目还剩多少"：直接要 kInBuf 会在最后一个条目上读出 data
            // 缓冲区外面去（QFile::write 返回 -1，实测就是这么炸的）
            const qint64 want = qMin(qint64(kInBuf), size - written);
            const qint64 n = w.write(data.constData() + off + written, int(want));
            if (n <= 0) {
                res.error = QStringLiteral("写入失败：%1").arg(outPath);
                return res;
            }
            written += n;
            if (report && !report(Archive::Progress{ off + written, total, name })) {
                res.cancelled = true;
                res.error = QStringLiteral("已取消，已解出来的文件留在：%1").arg(nativeDest);
                return res;
            }
            if (cancel && cancel()) {
                res.cancelled = true;
                res.error = QStringLiteral("已取消，已解出来的文件留在：%1").arg(nativeDest);
                return res;
            }
        }
        w.close();
        off += size;
        ++res.files;
        res.bytes += size;
    }
    res.dirs = rootObj.value(QStringLiteral("dirs")).toArray().size();
    res.ok = true;
    res.target = nativeDest;
    return res;
}

} // namespace Xz
