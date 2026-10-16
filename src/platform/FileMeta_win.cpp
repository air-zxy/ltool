#include "platform/FileMeta.h"

#include <QDir>
#include <QImage>

#include <windows.h>
#include <shellapi.h>

namespace Platform {

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

} // namespace Platform
