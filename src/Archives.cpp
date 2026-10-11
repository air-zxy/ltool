#include "Archives.h"

#include "SevenZip.h"
#include "Utils.h"

#include <QDir>
#include <QFileInfo>
#include <QDirIterator>
#include <QSaveFile>

#include <archive.h>
#include <archive_entry.h>

#include <QStringDecoder>

#include <QThread>

#include <string>

namespace {

constexpr int kBuf = 1 << 16;

// 名字编码：格式自己知道的信息优先
QString entryName(struct archive_entry* e)
{
    // 1) libarchive 明确标了"这个名是 UTF-8"的（zip 带标志位、7z）：直接按 UTF-8 用
    const char* u8 = archive_entry_pathname_utf8(e);
    if (u8 && *u8)
        return QString::fromUtf8(u8);
    // 2) 窄字符名：tar 这类格式存的就是 UTF-8，先严格验一遍再当 UTF-8 用
    const char* narrow = archive_entry_pathname(e);
    if (narrow && *narrow) {
        QStringDecoder utf8(QStringConverter::Utf8);
        const QString s = utf8.decode(QByteArrayView(narrow, int(qstrlen(narrow))));
        if (!utf8.hasError())
            return s;
    }
    // 3) 前两条都拿不到（名字在本机码页里转不出 UTF-8）：退到宽字符，再退到原样字节
    const wchar_t* w = archive_entry_pathname_w(e);
    if (w && *w)
        return QString::fromWCharArray(w);
    return narrow ? QString::fromLocal8Bit(narrow) : QString();
}

// 告诉 libarchive 每种格式的条目名"没标记时该按什么码页读"。
// 它默认一律按 C 运行时 locale（本机 = GBK）猜，tar 里好好的 UTF-8 中文名会被猜成乱码；
// 而国内工具做的 zip 不带 UTF-8 标志位，名字确实是 GBK，按 UTF-8 读同样糊。
// 必须在 archive_read_open 之前调用。
void applyCharsetHints(struct archive* a)
{
    archive_read_set_options(a, "tar:hdrcharset=UTF-8");
    archive_read_set_options(a, "cpio:hdrcharset=UTF-8");
    archive_read_set_options(a, "zip:hdrcharset=CP_ACP");
    archive_read_set_options(a, "rar:hdrcharset=CP_ACP");
    archive_read_set_options(a, "lha:hdrcharset=CP_ACP");
}

// libarchive 有时只给状态码不给文字（错误串是空的），界面上就会出现
// "读压缩包出错：" 这种没法查的话，所以兜一个 errno。
QString libMessage(struct archive* a, const QString& fallback)
{
    const char* s = archive_error_string(a);
    if (s && *s)
        return QString::fromUtf8(s);
    const int e = archive_errno(a);
    return e ? QStringLiteral("%1（错误码 %2）").arg(fallback).arg(e) : fallback;
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
    // 没扩展名的也不少：让 libarchive 自己认一次头部。
    // 这里用 format_all 就够了：它不包含 raw 兜底（实测单文件的 foo.gz 会被它判成
    // "不是压缩包"），所以不会出现"任何文件都算压缩包"，解压那边才需要单独把 raw 加上。
    struct archive* a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    applyCharsetHints(a);
    // 路径必须走 _w 版：窄字符版是 libarchive 用 C 运行时 locale（GBK）解的，
    // 而我们手里的 QByteArray 是 UTF-8 —— 中文路径喂进去直接就打不开了
    const std::wstring srcW = QDir::toNativeSeparators(path).toStdWString();
    const int r = archive_read_open_filename_w(a, srcW.c_str(), kBuf);
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
    // format_all 里不含 raw：光一个 foo.gz / foo.xz（没有容器，只有一层过滤器）
    // 要靠 raw 兜底才能把里面那个唯一的文件解出来
    archive_read_support_format_raw(a);
    applyCharsetHints(a);
    const std::wstring srcW = QDir::toNativeSeparators(archivePath).toStdWString();
    if (archive_read_open_filename_w(a, srcW.c_str(), kBuf) != ARCHIVE_OK) {
        res.error = QStringLiteral("打不开这个压缩包：%1").arg(libMessage(a, QStringLiteral("文件读不了")));
        archive_read_free(a);
        return res;
    }

    qint64 totalBytes = 0;                 // 边解边累加，见下面循环里的修正
    char buf[kBuf];
    qint64 done = 0;
    bool stopped = false;
    struct archive_entry* entry = nullptr;
    for (;;) {
        const int hr = archive_read_next_header(a, &entry);
        if (hr == ARCHIVE_EOF)
            break;
        if (hr < ARCHIVE_WARN) {
            res.error = QStringLiteral("读压缩包出错：%1").arg(libMessage(a, QStringLiteral("文件有问题")));
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
            res.error = QStringLiteral("写不出来：%1（压缩包里的条目名：%2）").arg(outPath, name);
            stopped = true;
            break;
        }
        la_ssize_t n = 0;
        qint64 written = 0;
        bool ioError = false;
        while ((n = archive_read_data(a, buf, sizeof(buf))) > 0) {
            if (out.write(buf, int(n)) != n) {
                ioError = true;
                break;
            }
            done += n;
            written += n;
            if (report && !report(Progress{ done, totalBytes, name })) {
                res.cancelled = true;             // 界面那边只会因为"按了取消"而返回 false
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
        if (totalBytes < done + qMax<qint64>(0, archive_entry_size(entry)))
            totalBytes = done + qMax<qint64>(0, archive_entry_size(entry));   // 至少不小于已解出的量
        if (ioError || stopped) {
            if (ioError)
                res.error = QStringLiteral("写入失败（磁盘满了？）：%1").arg(outPath);
            break;
        }
        ++res.files;
        res.bytes += written;
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
                const Report& report, const Cancel& cancel, int level)
{
    // 7z 不进 libarchive：它的 7z 写出器 LZMA1/LZMA2 恒为单线程（threads 那个选项只
    // 作用于 zstd），压同样的东西比 7-Zip 慢六倍。改成直接用 7-Zip 自己的编码器。
    if (sevenZip)
        return SevenZip::compress(inputs, destFile, level, report, cancel);
    Result res;
    const QString nativeDest = QDir::toNativeSeparators(destFile);
    struct archive* a = archive_write_new();
    // zip 走 libarchive 的默认（deflate，6 档，单线程）：以前这里写的是
    // set_format_option("zip","compression","level=6")，键值都不对，是个空操作
    archive_write_set_format_zip(a);
    archive_write_set_options(a, "zip:encoding=UTF-8");
    // 先写成 .part，成功后才改名成最终名字：直接用最终名字的话，中途桌面上就躺着一个
    // 0 字节的"成品"（libarchive 的写出器要到最后才落盘），看着像压坏了，其实还在跑，
    // 这时候去删还会被 Windows 报"文件已在 ltool.exe 中打开"。
    const QString partPath = nativeDest + QStringLiteral(".part");
    const std::wstring destW = partPath.toStdWString();
    if (archive_write_open_filename_w(a, destW.c_str()) != ARCHIVE_OK) {
        res.error = QStringLiteral("建不出压缩包：%1").arg(libMessage(a, QStringLiteral("目标路径写不了")));
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
            const QByteArray relUtf8 = rel.toUtf8();
            // 用 _utf8 版设置器：这样 zip 会带上"名字是 UTF-8"的标志位，
            // 中文条目名换别的解压程序也不会糊（update_pathname_utf8 不行，
            // 它要往 C 运行时 locale 转一遍，Qt 程序里那一步必失败）
            archive_entry_set_pathname_utf8(e, relUtf8.constData());
            if (mf.isDir()) {
                archive_entry_set_filetype(e, AE_IFDIR);
                archive_entry_set_perm(e, 0755);
                if (archive_write_header(a, e) != ARCHIVE_OK) {
                    res.error = QStringLiteral("目录 %1 写不进去：%2")
                                    .arg(QFileInfo(m).fileName(), libMessage(a, QStringLiteral("libarchive 拒绝了这条目录项")));
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
                res.error = QStringLiteral("%1 写不进去：%2").arg(m, libMessage(a, QStringLiteral("libarchive 拒绝了这一项")));
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
                    res.cancelled = true;         // 界面只有在按了取消时才返回 false
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
    const int closeRc = archive_write_close(a);
    const QString closeMsg = closeRc == ARCHIVE_OK ? QString()
                                                   : libMessage(a, QStringLiteral("压缩包没写完"));
    archive_write_free(a);
    res.bytes = done;
    if (res.cancelled) {
        QFile::remove(partPath);
        res.error = QStringLiteral("已取消，半成品压缩包已删掉");
        return res;
    }
    if (stopped) {
        QFile::remove(partPath);            // 失败的半成品别留在旁边装成成品
        return res;
    }
    if (closeRc != ARCHIVE_OK) {
        QFile::remove(partPath);
        res.error = QStringLiteral("收尾写头部失败：%1").arg(closeMsg);
        return res;
    }
    // 刚关掉的这一瞬间，杀毒/索引常常还抓着这个新文件，改名会失败 —— 给它几秒
    QString renameErr;
    if (!Utils::renameStaged(partPath, nativeDest, &renameErr)) {
        res.error = renameErr;
        res.target = partPath;
        return res;
    }
    res.ok = true;
    res.target = nativeDest;
    return res;
}

} // namespace Archive
