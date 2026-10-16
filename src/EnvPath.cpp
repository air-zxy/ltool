#include "EnvPath.h"
#include "platform/Paths.h"
#include "Utils.h"

#include <QDir>
#include <QFileInfo>

#include <windows.h>
#include <thread>

namespace {

bool readRawUserPath(QString* out)
{
    wchar_t buf[32768] = {};
    DWORD size = sizeof(buf);
    DWORD type = 0;
    LSTATUS st = RegGetValueW(HKEY_CURRENT_USER, L"Environment", L"Path",
                              RRF_RT_REG_EXPAND_SZ | RRF_RT_REG_SZ | RRF_NOEXPAND,
                              &type, buf, &size);
    if (st != ERROR_SUCCESS) {
        if (out)
            out->clear();
        return st == ERROR_FILE_NOT_FOUND || st == ERROR_SUCCESS;
    }
    if (out) {
        *out = QString::fromWCharArray(buf, size / 2);
        while (out->endsWith(QChar(0)))
            out->chop(1);
    }
    return true;
}

bool writeUserPath(const QString& raw, QString* err)
{
    HKEY k = nullptr;
    LSTATUS st = RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_SET_VALUE, &k);
    if (st != ERROR_SUCCESS) {
        if (err)
            *err = QString("打开注册表失败 (错误 %1)").arg(st);
        return false;
    }
    std::wstring w = raw.toStdWString();
    st = RegSetValueExW(k, L"Path", 0, REG_EXPAND_SZ,
                        (const BYTE*)w.c_str(), (DWORD)((w.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
    if (st != ERROR_SUCCESS) {
        if (err)
            *err = QString("写入注册表失败 (错误 %1)").arg(st);
        return false;
    }
    EnvPath::syncProcessPath();
    EnvPath::broadcastChange();
    return true;
}

QStringList splitRaw(const QString& raw)
{
    QStringList parts = raw.split(';', Qt::KeepEmptyParts);
    for (QString& p : parts)
        p = p.trimmed();
    return parts;
}

bool sameDir(const QString& a, const QString& b)
{
    QString ea = QDir::toNativeSeparators(QDir::cleanPath(Platform::expandEnv(a)));
    QString eb = QDir::toNativeSeparators(QDir::cleanPath(Platform::expandEnv(b)));
    return QString::compare(ea, eb, Qt::CaseInsensitive) == 0;
}

} // namespace

namespace EnvPath {

// 把注册表里的 PATH（机器 + 用户，展开 %VAR%）重新灌进本进程。
// 新开的终端靠 WM_SETTINGCHANGE 就能拿到新值，但 ltool 自己 spawn 的子进程
// （rustup / nvm / 定时任务脚本）继承的是本进程启动时那份，不补这一步就"装了却找不到命令"。
QString registryPath()
{
    const auto readReg = [](HKEY hive, const wchar_t* sub) {
        wchar_t buf[32768] = {};
        DWORD size = sizeof(buf), type = 0;
        if (RegGetValueW(hive, sub, L"Path", RRF_RT_REG_EXPAND_SZ | RRF_RT_REG_SZ | RRF_NOEXPAND,
                         &type, buf, &size) != ERROR_SUCCESS)
            return QString();
        QString v = QString::fromWCharArray(buf, size / 2);
        while (v.endsWith(QChar(0)))
            v.chop(1);
        return Platform::expandEnv(v).trimmed();
    };
    const QString machine = readReg(HKEY_LOCAL_MACHINE,
                                    L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment");
    const QString user = readReg(HKEY_CURRENT_USER, L"Environment");
    const QString merged = machine.isEmpty() ? user
                        : (user.isEmpty() ? machine : machine + ';' + user);
    return QDir::toNativeSeparators(merged);
}

void syncProcessPath()
{
    const QString merged = registryPath();
    if (!merged.isEmpty())
        qputenv("PATH", merged.toLocal8Bit());
}

QStringList userPathEntries()
{
    QString raw;
    readRawUserPath(&raw);
    QStringList parts = splitRaw(raw);
    parts.removeAll(QString());
    return parts;
}

bool userPathContains(const QString& dir)
{
    const QStringList parts = userPathEntries();
    for (const QString& p : parts)
        if (sameDir(p, dir))
            return true;
    return false;
}

bool addToUserPath(const QString& dir, QString* err)
{
    QString d = QDir::toNativeSeparators(QDir::cleanPath(dir));
    if (d.isEmpty() || !QFileInfo::exists(d)) {
        if (err)
            *err = QString("目录不存在：%1").arg(d);
        return false;
    }
    if (userPathContains(d)) {
        if (err)
            *err = "该目录已在 PATH 中";
        return false;
    }
    QString raw;
    readRawUserPath(&raw);
    QString newRaw = raw.trimmed();
    if (!newRaw.isEmpty() && !newRaw.endsWith(';'))
        newRaw += ';';
    newRaw += d;
    return writeUserPath(newRaw, err);
}

bool removeFromUserPath(const QString& dir, QString* err)
{
    QString raw;
    readRawUserPath(&raw);
    QStringList parts = splitRaw(raw);
    QStringList kept;
    bool removed = false;
    for (const QString& p : parts) {
        if (!p.isEmpty() && sameDir(p, dir)) {
            removed = true;
            continue;
        }
        kept.append(p);
    }
    if (!removed) {
        if (err)
            *err = "PATH 中没有该目录";
        return false;
    }
    return writeUserPath(kept.join(';'), err);
}

bool setUserEnv(const QString& name, const QString& value, QString* err)
{
    HKEY k = nullptr;
    LSTATUS st = RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_SET_VALUE, &k);
    if (st != ERROR_SUCCESS) {
        if (err) *err = QString("打开注册表失败 (错误 %1)").arg(st);
        return false;
    }
    const std::wstring wn = name.toStdWString();
    const std::wstring wv = value.toStdWString();
    st = RegSetValueExW(k, wn.c_str(), 0, REG_SZ,
                        (const BYTE*)wv.c_str(), (DWORD)((wv.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
    if (st != ERROR_SUCCESS) {
        if (err) *err = QString("写入环境变量失败 (错误 %1)").arg(st);
        return false;
    }
    broadcastChange();
    return true;
}

bool removeUserEnv(const QString& name, QString* err)
{
    HKEY k = nullptr;
    LSTATUS st = RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_SET_VALUE, &k);
    if (st != ERROR_SUCCESS) {
        if (err) *err = QString("打开注册表失败 (错误 %1)").arg(st);
        return false;
    }
    const std::wstring wn = name.toStdWString();
    st = RegDeleteValueW(k, wn.c_str());
    RegCloseKey(k);
    if (st != ERROR_SUCCESS && st != ERROR_FILE_NOT_FOUND) {
        if (err) *err = QString("删除环境变量失败 (错误 %1)").arg(st);
        return false;
    }
    broadcastChange();
    return true;
}

void broadcastChange()
{
    // HWND_BROADCAST 要等每个顶层窗口回应（最长 3s）。在 GUI 线程上调用会让"切换版本"
    // 明显卡顿（开着游戏/浏览器时更糟），所以丢给后台线程，晚一点广播不影响新开的终端。
    std::thread([] {
        SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                            (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 3000, nullptr);
    }).detach();
}

} // namespace EnvPath
