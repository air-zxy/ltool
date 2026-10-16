#include "platform/Launch.h"

#include <QDir>
#include <QFileInfo>

#include <windows.h>
#include <shellapi.h>

namespace Platform {

bool launchAsAdmin(const QString& path, const QString& params, const QString& workingDir)
{
    if (path.isEmpty())
        return false;
    QString file = QDir::toNativeSeparators(path);
    QString wd = workingDir;
    if (wd.isEmpty())
        wd = QFileInfo(path).absolutePath();
    if (!QFileInfo(wd).isDir())
        wd.clear();

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_ASYNCOK;
    sei.lpVerb = L"runas";
    sei.lpFile = (LPCWSTR)file.utf16();
    if (!params.isEmpty())
        sei.lpParameters = (LPCWSTR)params.utf16();
    if (!wd.isEmpty()) {
        wd = QDir::toNativeSeparators(wd);
        sei.lpDirectory = (LPCWSTR)wd.utf16();
    }
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != FALSE;
}

bool launch(const QString& path, const QString& params, const QString& workingDir, bool async)
{
    if (path.isEmpty())
        return false;
    QString file = QDir::toNativeSeparators(path);
    QString wd = workingDir;
    if (wd.isEmpty())
        wd = QFileInfo(path).absolutePath();
    if (!QFileInfo(wd).isDir())
        wd.clear();

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_FLAG_NO_UI;
    if (async)
        sei.fMask |= SEE_MASK_ASYNCOK;   // 交给 shell 异步跑，不等 DDE/进程起来
    sei.lpVerb = L"open";
    sei.lpFile = (LPCWSTR)file.utf16();
    if (!params.isEmpty())
        sei.lpParameters = (LPCWSTR)params.utf16();
    if (!wd.isEmpty()) {
        wd = QDir::toNativeSeparators(wd);
        sei.lpDirectory = (LPCWSTR)wd.utf16();
    }
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != FALSE;
}

void reveal(const QString& path)
{
    QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    QString params = "/select,\"" + native + "\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", (LPCWSTR)params.utf16(), nullptr, SW_SHOWNORMAL);
}

} // namespace Platform
