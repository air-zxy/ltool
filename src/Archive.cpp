#include "Archive.h"

#include "Utils.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

#include <archive.h>
#include <archive_entry.h>

#include <cstring>

namespace {

constexpr int kBuf = 1 << 16;

bool isZip(const QString& path)
{
    return path.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive);
}

// 条目名：优先宽字符（zip 里的文件名编码五花八门，libarchive 会尽量转成 UTF-16）
QString entryName(struct archive_entry* e)
{
    const wchar_t* w = archive_entry_pathname_w(e);
    if (w && *w)
        return QString::fromWCharArray(w);
    const char* a = archive_entry_pathname(e);
    return a ? QString::fromUtf8(a) : QString();
}

// 挡一手"压缩包把文件写到目录外面"（zip slip）
bool safeUnder(const QString& root, const QString& target)
{
    const QString r = QDir::toNativeSeparators(root).toLower();
    const QString t = QDir::toNativeSeparators(target).toLower();
    if (t == r)
        return true;
    return t.startsWith(r + QLatin1Char('\\'));
}

} // namespace

namespace Archive {

QString backendHint()
{
    return QStringLiteral("libarchive %1（读：zip / 7z / tar / gz / bz2 / xz / zstd / iso；"
                          "写：zip 和 7z。RAR 只能读而且不完全支持，写不了）")
        .arg(QString::fromLatin8(archive_version_string()));
}

bool looksLikeArchive(const QString& path)
{
    static const char* kExt[] = {
        ".zip", ".7z", ".tar", ".tar.gz", ".tgz", ".tar.bz2", ".tbz2", ".tar.xz", ".txz",
        ".tar.zst", ".gz", ".bz2", ".xz", ".zst", ".lz4", ".iso", ".cab", ".cpio", ".shar",
        ".war", ".jar", ".xpi", ".whl", ".rar", ".apk", ".vhd"
    };
    const QString lower = path.toLower();
    for (const char* e : kExt)
        if (lower.endsWith(QLatin1String(e)))
            return true;
    // 没扩展名的也不少：让 libarchive 自己认一次头部
    struct archive* a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    const int r = archive_read_open_filename(a, QDir::toNativeSeparators(path).toUtf8().constData(), kBuf);
    bool ok = false;
    if (r == ARCHIVE_OK) {
        struct archive_entry* e = nullptr;
        ok = archive_read_next_header(a, &e) == ARCHIVE_OK;
    }
    archive_read_free(a);
    return ok;
}

Result extract(const QString& archivePath, const QString& destDir, const Report& report, const Cancel& cancel)
{
    Result res;
    const QString nativeDest = QDir::toNativeSeparators(destDir);
    if (!QDir().mkpath(nativeDest)) {
        res.error = QStringLiteral("建不出目标目录：%1").arg(nativeDest);
        return res;
    }
    struct archive* a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    const QByteArray srcUtf8 = QDir::toNativeSeparators(archivePath).toUtf8();
    if (archive_read_open_filename(a, srcUtf8.constData(), kBuf) != ARCHIVE_OK) {
        res.error = QStringLiteral("打不开这个压缩包：%1").arg(QString::fromUtf8(archive_error_string(a)));
        archive_read_free(a);
        return res;
    }
    // zip 里的中文名可能带 UTF-8 标志，也可能就是本机 ANSI（GBK）；
    // 前者 libarchive 自己会认，后者要告诉它按本地编码解
    if (isZip(archivePath))
        archive_read_set_options(a, "hdrcharset=CP936");

    qint64 totalBytes = 0;
    {
        // 先扫一遍拿总量：有些格式（tar/gz）头部就带大小，zip 也有
        struct archive* probe = archive_read_new();
        archive_read_support_filter_all(probe);
        archive_read_support_format_all(probe);
        if (archive_read_open_filename(probe, srcUtf8.constData(), kBuf) == ARCHIVE_OK) {
            if (isZip(archivePath))
                archive_read_set_options(probe, "hdrcharset=CP936");
            struct archive_entry* e = nullptr;
            while (archive_read_next_header(probe, &e) == ARCHIVE_OK) {
                const la_int64_t sz = archive_entry_size(e);
                if (sz > 0)
                    totalBytes += sz;
                archive_read_header_next_skip_all:;   // 标签占位，下面用正规 API 跳过
                if (archive_read_data_skip(probe) != ARCHIVE_OK)
                    break;
            }
        }
        archive_read_free(probe);
    }

    char buf[kBuf];
    qint64 done = 0;
    bool stopped = false;
    struct archive_entry* entry = nullptr;
    for (;;) {
        const int hr = archive_read_next_header(a, &entry);
        if (hr == ARCHIVE_EOF)
            break;
        if (hr < ARCHIVE_WARN) {
            res.error = QStringLiteral("读压缩包出错：%1").arg(QString::fromUtf8(archive_error_string(a)));
            stopped = true;
            break;
        }
        const QString name = entryName(entry);
        const int mode = int(archive_entry_filetype(entry));
        if (mode == AE_IFLNK || (mode != AE_IFDIR && mode != AE_IFREG)) {
            archive_read_data_skip(a);          // 符号链接/设备节点这类一律不解，避免指向盘外
            continue;
        }
        const QString outPath = QDir::cleanPath(nativeDest + QLatin1Char('/') + name);
        if (!safeUnder(nativeDest, outPath)) {
            archive_read_data_skip(a);
            continue;
        }
        if (mode == AE_IFDIR) {
            QDir().mkpath(outPath);
            ++res.dirs;
            continue;
        }
        QDir().mkpath(QFileInfo(outPath).absolutePath());
        QFile out(outPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            res.error = QStringLiteral("写不出来：%1").arg(outPath);
            stopped = true;
            break;
        }
        la_ssize_t n = 0;
        bool ioError = false;
        while ((n = archive_read_data(a, buf, sizeof(buf))) > 0) {
            if (out.write(buf, int(n)) != n) {
                ioError = true;
                break;
            }
            done += n;
            if (report && !report(Progress{ done, totalBytes, name })) {
                stopped = true;
                break;
            }
            if (cancel && cancel()) {
                res.cancelled = true;
                stopped = true;
                break;
            }
        }
        out.close();
        if (ioError || stopped) {
            if (ioError)
                res.error = QStringLiteral("写入失败（磁盘满了？）：%1").arg(outPath);
            break;
        }
        ++res.files;
        res.bytes += done;
        if (hr == ARCHIVE_WARN)
            ;   // 有警告（比如权限位没设上）但内容解出来了，继续
        archive_entry_set_perm(entry, 0644);
    }
    archive_read_close(a);
    archive_read_free(a);

    if (res.cancelled) {
        res.error = QStringLiteral("已取消，已解出来的文件留在：%1").arg(nativeDest);
        res.target = nativeDest;
        return res;
    }
    if (stopped)
        return res;
    res.ok = true;
    res.target = nativeDest;
    return res;
}

Result compress(const QStringList& inputs, const QString& destFile, bool sevenZip,
                const Report& report, const Cancel& cancel)
{
    Result res;
    const QString nativeDest = QDir::toNativeSeparators(destFile);
    struct archive* a = archive_write_new();
    if (sevenZip) {
        archive_write_add_filter_lzma(a);
        archive_write_set_format_7zip(a);
    } else {
        archive_write_add_filter_bzip2(a);   // zip 的 deflate 由格式自己管，这里加的是"外层过滤"
        archive_write_set_format_zip(a);
    }
    archive_write_set_format_option(a, "zip", "compression", "level=6");
    archive_write_set_options(a, "zip:encoding=UTF-8");
    if (archive_write_open_filename(a, nativeDest.toUtf8().constData()) != ARCHIVE_OK) {
        res.error = QStringLiteral("建不出压缩包：%1").arg(QString::fromUtf8(archive_error_string(a)));
        archive_write_free(a);
        return res;
    }

    qint64 totalBytes = 0;
    for (const QString& in : inputs) {
        const QFileInfo fi(in);
        if (fi.isDir()) {
            QDirIterator it(in, QDir::Files | QDir::NoSymLinks | QDir::Hidden | QDir::System,
                            QDirIterator::Subdirectories);
            while (it.hasNext())
                totalBytes += QFileInfo(it.next()).size();
        } else if (fi.exists()) {
            totalBytes += fi.size();
        }
    }

    char buf[kBuf];
    qint64 done = 0;
    bool stopped = false;
    for (const QString& in : inputs) {
        const QFileInfo rootInfo(in);
        if (!rootInfo.exists())
            continue;
        const QString parent = rootInfo.absolutePath();
        QStringList members;
        if (rootInfo.isDir()) {
            members << QDir::toNativeSeparators(in);
            QDirIterator dit(in, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                             QDirIterator::Subdirectories);
            while (dit.hasNext())
                members << QDir::toNativeSeparators(dit.next());
            QDirIterator fit(in, QDir::Files | QDir::NoSymLinks | QDir::Hidden | QDir::System,
                             QDirIterator::Subdirectories);
            while (fit.hasNext())
                members << QDir::toNativeSeparators(fit.next());
        } else {
            members << QDir::toNativeSeparators(in);
        }
        for (const QString& m : members) {
            if (cancel && cancel()) {
                res.cancelled = true;
                stopped = true;
                break;
            }
            const QFileInfo mf(m);
            struct archive_entry* e = archive_entry_new();
            const QString rel = QDir(parent).relativeFilePath(m).replace(QLatin1Char('\\'), QLatin1Char('/'));
            archive_entry_set_pathname(e, rel.toUtf8().constData());
            if (mf.isDir()) {
                archive_entry_set_filetype(e, AE_IFDIR);
                archive_entry_set_perm(e, 0755);
                if (archive_write_header(a, e) != ARCHIVE_OK) {
                    res.error = QString::fromUtf8(archive_error_string(a));
                    archive_entry_free(e);
                    stopped = true;
                    break;
                }
                ++res.dirs;
                archive_entry_free(e);
                continue;
            }
            QFile f(m);
            const qint64 sz = mf.size();
            archive_entry_set_filetype(e, AE_IFREG);
            archive_entry_set_perm(e, 0644);
            archive_entry_set_size(e, sz);
            archive_entry_set_mtime(e, mf.lastModified().toSecsSinceEpoch(), 0);
            if (archive_write_header(a, e) != ARCHIVE_OK) {
                res.error = QStringLiteral("%1: %2").arg(m, QString::fromUtf8(archive_error_string(a)));
                archive_entry_free(e);
                stopped = true;
                break;
            }
            archive_entry_free(e);
            if (!f.open(QIODevice::ReadOnly)) {
                res.error = QStringLiteral("读不了：%1").arg(m);
                stopped = true;
                break;
            }
            bool io = false;
            while (!f.atEnd()) {
                const QByteArray chunk = f.read(kBuf);
                if (chunk.isEmpty())
                    break;
                const la_ssize_t w = archive_write_data(a, chunk.constData(), size_t(chunk.size()));
                if (w != la_ssize_t(chunk.size())) {
                    res.error = QStringLiteral("写压缩包失败：%1").arg(m);
                    io = true;
                    break;
                }
                done += w;
                if (report && !report(Progress{ done, totalBytes, mf.fileName() })) {
                    stopped = true;
                    break;
                }
            }
            f.close();
            if (io || stopped)
                break;
            ++res.files;
        }
        if (stopped)
            break;
    }
    archive_write_close(a);
    archive_write_free(a);
    res.bytes = done;
    if (res.cancelled) {
        QFile::remove(nativeDest);
        res.error = QStringLiteral("已取消，半成品压缩包已删掉");
        return res;
    }
    if (stopped)
        return res;
    res.ok = true;
    res.target = nativeDest;
    return res;
}

} // namespace Archive
