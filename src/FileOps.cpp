#include "FileOps.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

#include <windows.h>

// 这里只剩"读一个文件占了多少"那两件（info / usage）—— 它们本身就是 Win32 的盘符、
// 簇、压缩大小那套，POSIX 上没有对等概念，所以这个 TU 不进可移植名单。
// 粉碎、谁占用着它、结束进程三件抽成了 platform/FileOps_{win,posix}.cpp（第七道缝），
// 因为 src/FileTools.cpp 已经在可移植名单里，链接期要在这道缝上找得到这三个符号。
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
    HANDLE h = CreateFileW(w.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        FILE_STANDARD_INFO si{};
        if (GetFileInformationByHandleEx(h, FileStandardInfo, &si, sizeof(si)))
            out.hardLinks = int(si.NumberOfLinks);
        BY_HANDLE_FILE_INFORMATION bh{};
        if (GetFileInformationByHandle(h, &bh))
            out.hardLinks = int(bh.nNumberOfLinks);
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
} // namespace FileOps
