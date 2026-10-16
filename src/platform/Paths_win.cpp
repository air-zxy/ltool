#include "platform/Paths.h"

#include <QDir>

#include <windows.h>
#include <shlobj.h>

namespace Platform {

QString knownDir(KnownDir d)
{
    KNOWNFOLDERID id = FOLDERID_Desktop;
    switch (d) {
    case KnownDir::UserDesktop:  id = FOLDERID_Desktop; break;
    case KnownDir::SharedDesktop: id = FOLDERID_PublicDesktop; break;
    case KnownDir::UserApps:     id = FOLDERID_Programs; break;
    case KnownDir::SystemApps:   id = FOLDERID_CommonPrograms; break;
    }
    PWSTR p = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DONT_VERIFY, nullptr, &p)))
        return {};
    QString r = QString::fromWCharArray(p);
    CoTaskMemFree(p);
    return QDir::fromNativeSeparators(r);
}

QString expandEnv(const QString& s)
{
    wchar_t buf[2048];
    DWORD n = ExpandEnvironmentStringsW((LPCWSTR)s.utf16(), buf, 2048);
    if (n == 0 || n > 2048)
        return s;
    QString r = QString::fromWCharArray(buf);
    while (r.endsWith(QChar(0)))
        r.chop(1);
    return r;
}

} // namespace Platform
