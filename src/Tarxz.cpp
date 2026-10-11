#include "Tarxz.h"

#include "Utils.h"

#include <lzma.h>

#include <archive.h>
#include <archive_entry.h>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QThread>

#include <algorithm>
#include <vector>

namespace {

constexpr int kInBuf = 1 << 16;
constexpr int kOutBuf = 1 << 20;
// 多线程是按 xz 的 Block 分活的：一个 2GB 的文件不切块就只有一条线程在跑；
// 块太小又会让每块各带一份字典，压缩率掉得多。取"每个线程约 2 块"，夹在 1~16MB。
constexpr uint64_t kBlockMin = 1ULL * 1024 * 1024;
constexpr uint64_t kBlockMax = 16ULL * 1024 * 1024;

QString lzErr(lzma_ret r)
{
    switch (r) {
    case LZMA_MEM_ERROR:         return QStringLiteral("内存不够");
    case LZMA_OPTIONS_ERROR:     return QStringLiteral("liblzma 不接受这组参数");
    case LZMA_UNSUPPORTED_CHECK: return QStringLiteral("这个 xz 流用了没开支持的校验");
    case LZMA_DATA_ERROR:        return QStringLiteral("数据坏了");
    case LZMA_BUF_ERROR:         return QStringLiteral("输入比声明的短");
    case LZMA_PROG_ERROR:        return QStringLiteral("调用 liblzma 的方式不对");
    default:                     return QStringLiteral("liblzma 返回 %1").arg(int(r));
    }
}

// 一条流水线：libarchive 的 tar 写出器（只打包不压缩）→ 它的输出回调 → 多线程 xz 编码器
// → 目标文件。全程不落临时文件。
struct Pipe
{
    QSaveFile* out = nullptr;
    lzma_stream strm = LZMA_STREAM_INIT;
    std::vector<char> ob;
    QString error;
    bool ok = true;

    Pipe() { ob.resize(kOutBuf); }

    void drain()
    {
        const qint64 have = qint64(ob.size()) - qint64(strm.avail_out);
        if (have <= 0)
            return;
        if (out->write(ob.data(), int(have)) != have) {
            error = QStringLiteral("写不出来（磁盘满了？）");
            ok = false;
            return;
        }
        strm.next_out = reinterpret_cast<uint8_t*>(ob.data());
        strm.avail_out = ob.size();
    }

    // 喂一段输入进去（LZMA_RUN：把手头这些吃完就回来）
    void feed(const uint8_t* data, size_t n)
    {
        strm.next_in = data;
        strm.avail_in = n;
        while (strm.avail_in > 0) {
            const lzma_ret r = lzma_code(&strm, LZMA_RUN);
            drain();
            if (!ok)
                return;
            if (r == LZMA_STREAM_END)
                return;
            if (r != LZMA_OK) {
                error = lzErr(r);
                ok = false;
                return;
            }
        }
    }

    // 收尾。liblzma 的规矩：一旦用了 FINISH 这类特殊动作，就得用同一个动作喂到
    // 它返回 LZMA_STREAM_END 为止，中途换动作就是 LZMA_PROG_ERROR(11)。
    void finish()
    {
        for (;;) {
            const lzma_ret r = lzma_code(&strm, LZMA_FINISH);
            drain();
            if (!ok)
                return;
            if (r == LZMA_STREAM_END)
                return;
            if (r != LZMA_OK) {
                error = lzErr(r);
                ok = false;
                return;
            }
        }
    }
};

la_ssize_t pipeWrite(struct archive*, void* client, const void* data, size_t len)
{
    Pipe& p = *static_cast<Pipe*>(client);
    p.feed(static_cast<const uint8_t*>(data), len);
    return p.ok ? la_ssize_t(len) : -1;
}

struct Job {
    QString path;
    QString name;      // 包里的路径（'/' 分隔，带输入那层的名字）
    qint64 size = 0;
};

struct Tree { QVector<Job> jobs; QStringList dirs; qint64 total = 0; };

Tree collect(const QStringList& inputs)
{
    Tree t;
    for (const QString& in0 : inputs) {
        // 一律先转绝对路径：QDirIterator 给的是"用什么路径进来就用什么出去"，
        // 传相对路径会让条目名带上整个相对前缀（实测 .lzx 就是这么坏的）
        const QString in = QFileInfo(in0).absoluteFilePath();
        const QFileInfo ri(in);
        if (!ri.exists())
            continue;
        const QString parent = ri.absolutePath();
        const auto relOf = [&parent](const QString& abs) {
            return QDir(parent).relativeFilePath(abs).replace(QLatin1Char('\\'), QLatin1Char('/'));
        };
        if (!ri.isDir()) {
            t.jobs.append({ ri.absoluteFilePath(), relOf(ri.absoluteFilePath()), ri.size() });
            continue;
        }
        QDirIterator dit(in, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                         QDirIterator::Subdirectories);
        while (dit.hasNext())
            t.dirs << relOf(dit.next());
        QDirIterator fit(in, QDir::Files | QDir::NoSymLinks | QDir::Hidden | QDir::System,
                         QDirIterator::Subdirectories);
        while (fit.hasNext()) {
            const QString f = fit.next();
            t.jobs.append({ f, relOf(f), QFileInfo(f).size() });
        }
    }
    std::sort(t.jobs.begin(), t.jobs.end(),
              [](const Job& a, const Job& b) { return a.name < b.name; });
    t.dirs.sort();
    t.dirs.removeDuplicates();
    for (const Job& j : t.jobs)
        t.total += j.size;
    return t;
}

} // namespace

namespace Tarxz {

Archive::Result compress(const QStringList& inputs, const QString& destFile, int threads,
                         const Archive::Report& report, const Archive::Cancel& cancel)
{
    Archive::Result res;
    const Tree tree = collect(inputs);
    if (tree.jobs.isEmpty()) {
        res.error = QStringLiteral("这里面没有文件");
        return res;
    }
    const int th = threads > 0 ? threads : qMax(1, qMin(16, QThread::idealThreadCount()));

    QSaveFile out(destFile);
    if (!out.open(QIODevice::WriteOnly)) {
        res.error = QStringLiteral("建不出 %1（路径不可写？）").arg(destFile);
        return res;
    }
    Pipe pipe;
    pipe.out = &out;

    lzma_mt opt {};
    opt.preset = 6;                       // 和 7-Zip 的 -mx=6 同一档
    opt.threads = quint32(th);
    opt.block_size = qint64(qBound<uint64_t>(kBlockMin, quint64(tree.total) / quint64(qMax(1, th * 2)),
                                             kBlockMax));
    if (lzma_stream_encoder_mt(&pipe.strm, &opt) != LZMA_OK) {
        lzma_end(&pipe.strm);
        out.cancelWriting();
        res.error = QStringLiteral("开不了多线程编码器：%1").arg(pipe.error.isEmpty()
                                                                    ? QStringLiteral("liblzma 拒绝了参数")
                                                                    : pipe.error);
        return res;
    }

    struct archive* a = archive_write_new();
    archive_write_set_format_pax_restricted(a);   // pax：条目名按 UTF-8 存，中文名不糊
    archive_write_add_filter_none(a);             // tar 这层不压缩，压缩是下面 xz 的事
    archive_write_open(a, &pipe, nullptr, pipeWrite, nullptr);

    std::vector<char> ib;
    ib.resize(kInBuf);
    qint64 done = 0;
    bool stopped = false;

    const auto addDir = [&](const QString& name) {
        struct archive_entry* e = archive_entry_new();
        const QByteArray u8 = (name + QLatin1Char('/')).toUtf8();
        archive_entry_set_pathname_utf8(e, u8.constData());
        archive_entry_set_filetype(e, AE_IFDIR);
        archive_entry_set_perm(e, 0755);
        archive_write_header(a, e);
        archive_entry_free(e);
    };
    for (const QString& d : tree.dirs)
        addDir(d);

    for (const Job& j : tree.jobs) {
        QFile f(j.path);
        if (!f.open(QIODevice::ReadOnly)) {
            res.error = QStringLiteral("读不了：%1").arg(j.path);
            stopped = true;
            break;
        }
        struct archive_entry* e = archive_entry_new();
        const QByteArray u8 = j.name.toUtf8();
        archive_entry_set_pathname_utf8(e, u8.constData());
        archive_entry_set_filetype(e, AE_IFREG);
        archive_entry_set_perm(e, 0644);
        archive_entry_set_size(e, j.size);
        archive_entry_set_mtime(e, QFileInfo(j.path).lastModified().toSecsSinceEpoch(), 0);
        if (archive_write_header(a, e) != ARCHIVE_OK) {
            res.error = QStringLiteral("%1 写不进去：%2").arg(j.name,
                            QString::fromUtf8(archive_error_string(a)));
            archive_entry_free(e);
            f.close();
            stopped = true;
            break;
        }
        archive_entry_free(e);
        for (;;) {
            const qint64 got = f.read(ib.data(), qint64(ib.size()));
            if (got <= 0)
                break;
            if (archive_write_data(a, ib.data(), size_t(got)) < 0 || !pipe.ok) {
                res.error = pipe.error.isEmpty() ? QStringLiteral("写 tar 失败：%1").arg(j.name)
                                                 : pipe.error;
                stopped = true;
                break;
            }
            done += got;
            if (report && !report(Archive::Progress{ done, tree.total, j.name })) {
                res.cancelled = true;      // 界面那边只有按了取消才返回 false
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
        ++res.files;
    }
    res.dirs = tree.dirs.size();
    res.bytes = done;
    archive_write_close(a);
    archive_write_free(a);
    if (!stopped)
        pipe.finish();
    lzma_end(&pipe.strm);

    if (res.cancelled) {
        out.cancelWriting();
        res.error = QStringLiteral("已取消，半成品已删掉");
        return res;
    }
    if (stopped || !pipe.ok) {
        out.cancelWriting();
        if (res.error.isEmpty())
            res.error = pipe.error;
        return res;
    }
    if (!out.commit()) {
        res.error = QStringLiteral("%1 没写成（磁盘满，或者被别的程序占着）").arg(destFile);
        return res;
    }
    Utils::log(QStringLiteral("tarxz: 压完 %1 个文件 / %2 字节 → %3，线程=%4，块=%5 MB")
                   .arg(res.files).arg(done).arg(QDir::toNativeSeparators(destFile)).arg(th)
                   .arg(int(opt.block_size / 1048576)));
    res.ok = true;
    res.target = QDir::toNativeSeparators(destFile);
    return res;
}

} // namespace Tarxz
