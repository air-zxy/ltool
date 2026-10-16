#include "RegClean.h"
#include "platform/Paths.h"
#include "Utils.h"

#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStringList>
#include <QTextStream>

#include <cstring>

#include <windows.h>

namespace {

struct HiveKey {
    HKEY root;
    const wchar_t* hiveName;
    const wchar_t* sub;
};

QString readString(HKEY k, const wchar_t* name)
{
    DWORD type = 0, size = 0;
    if (RegQueryValueExW(k, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS)
        return {};
    if (type != REG_SZ && type != REG_EXPAND_SZ)
        return {};
    QByteArray buf(int(size), 0);
    if (RegQueryValueExW(k, name, nullptr, &type, (BYTE*)buf.data(), &size) != ERROR_SUCCESS)
        return {};
    QString s = QString::fromWCharArray((const wchar_t*)buf.constData(), size / 2);
    while (s.endsWith(QChar(0)))
        s.chop(1);
    return s.trimmed();
}

DWORD readDword(HKEY k, const wchar_t* name, DWORD def = 0)
{
    DWORD v = def, sz = sizeof(v), t = 0;
    if (RegQueryValueExW(k, name, nullptr, &t, (BYTE*)&v, &sz) != ERROR_SUCCESS || t != REG_DWORD)
        return def;
    return v;
}

// 命令行取 exe、判断目标在不在：和开机启动分析共用 Utils 里那一份实现，
// 两边规则必须一致，不然同一个键在一边算失效、另一边不算。
QString extractExeFromCommand(const QString& cmd)
{
    return Utils::exeFromCommand(cmd);
}

// Windows 自己的文件（%SystemRoot% 下）不报成"残留"：就算文件真没了，
// 删那个键也不是正确修法（该跑 sfc / 重装组件），而且会误伤一堆系统组件。
bool underWindowsDir(const QString& path)
{
    static const QString root = Platform::expandEnv(QStringLiteral("%SystemRoot%")).toLower();
    if (root.isEmpty())
        return false;
    return Platform::expandEnv(path).toLower().startsWith(root);
}

bool fileExistsSmart(const QString& rawPath)
{
    return !rawPath.isEmpty() && !Utils::fileTargetMissing(rawPath);
}

void scanUninstall(const HiveKey& hk, QVector<RegIssue>* out)
{
    HKEY k = nullptr;
    if (RegOpenKeyExW(hk.root, hk.sub, 0, KEY_READ, &k) != ERROR_SUCCESS)
        return;
    wchar_t name[256];
    DWORD nameLen = 256;
    DWORD idx = 0;
    while (RegEnumKeyExW(k, idx++, name, &nameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        nameLen = 256;
        const QString subName = QString::fromWCharArray(name);
        HKEY sk = nullptr;
        if (RegOpenKeyExW(k, name, 0, KEY_READ, &sk) == ERROR_SUCCESS) {
            const QString disp = readString(sk, L"DisplayName");
            if (!disp.isEmpty() && readDword(sk, L"SystemComponent", 0) != 1) {
                const QString un = readString(sk, L"UninstallString");
                const QString icon = readString(sk, L"DisplayIcon");
                const QString instLoc = readString(sk, L"InstallLocation");
                bool orphan = false;
                QString why;
                if (un.startsWith("MsiExec", Qt::CaseInsensitive)) {
                    // MSI 自身管理，跳过
                } else if (!un.isEmpty()) {
                    const QString exe = extractExeFromCommand(un);
                    if (!exe.isEmpty() && exe.contains(':')
                        && !fileExistsSmart(exe)
                        && (icon.isEmpty() || !fileExistsSmart(icon.section(',', 0, 0)))) {
                        orphan = true;
                        why = QString("卸载程序不存在：%1").arg(exe);
                    }
                } else if (!instLoc.isEmpty() && instLoc.contains(':')) {
                    if (!fileExistsSmart(instLoc)) {
                        orphan = true;
                        why = QString("安装目录不存在：%1").arg(instLoc);
                    }
                }
                if (orphan) {
                    RegIssue it;
                    it.kind = RegIssue::Kind::UninstallOrphan;
                    // 删了不会卸载任何东西，只是把它从"应用和功能"列表里藏掉 —— 归到谨慎
                    it.risk = RegIssue::Risk::Careful;
                    it.isKey = true;
                    it.hiveName = QString::fromWCharArray(hk.hiveName);
                    it.subKey = QString::fromWCharArray(hk.sub) + "\\" + subName;
                    it.title = disp;
                    it.detail = why;
                    out->append(it);
                }
            }
            RegCloseKey(sk);
        }
    }
    RegCloseKey(k);
}

void scanRun(const HiveKey& hk, QVector<RegIssue>* out)
{
    HKEY k = nullptr;
    if (RegOpenKeyExW(hk.root, hk.sub, 0, KEY_READ, &k) != ERROR_SUCCESS)
        return;
    DWORD vals = 0;
    RegQueryInfoKeyW(k, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &vals,
                     nullptr, nullptr, nullptr, nullptr);
    for (DWORD i = 0; i < vals; ++i) {
        wchar_t name[512];
        DWORD nameLen = 512;
        DWORD type = 0;
        BYTE data[4096];
        DWORD dataSize = sizeof(data) - 2;
        if (RegEnumValueW(k, i, name, &nameLen, nullptr, &type, data, &dataSize) != ERROR_SUCCESS)
            continue;
        if (type != REG_SZ && type != REG_EXPAND_SZ)
            continue;
        data[dataSize] = 0;
        data[dataSize + 1] = 0;
        const QString valName = QString::fromWCharArray(name, nameLen);
        const QString cmd = QString::fromWCharArray((const wchar_t*)data);
        const QString exe = extractExeFromCommand(cmd);
        if (exe.isEmpty())
            continue;
        if (exe.startsWith('@') || exe.startsWith('{'))
            continue; // UWP/包引用
        if (fileExistsSmart(exe))
            continue;
        RegIssue it;
        it.kind = RegIssue::Kind::RunOrphan;
        it.isKey = false;
        it.hiveName = QString::fromWCharArray(hk.hiveName);
        it.subKey = QString::fromWCharArray(hk.sub);
        it.valueName = valName;
        it.title = valName;
        it.detail = QString("启动目标不存在：%1").arg(exe);
        out->append(it);
    }
    RegCloseKey(k);
}

void scanMuiCache(QVector<RegIssue>* out)
{
    const wchar_t* sub = L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\Shell\\MuiCache";
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, sub, 0, KEY_READ, &k) != ERROR_SUCCESS)
        return;
    DWORD vals = 0;
    RegQueryInfoKeyW(k, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &vals,
                     nullptr, nullptr, nullptr, nullptr);
    for (DWORD i = 0; i < vals; ++i) {
        wchar_t name[1024];
        DWORD nameLen = 1024;
        DWORD type = 0;
        BYTE data[4096];
        DWORD dataSize = sizeof(data) - 2;
        if (RegEnumValueW(k, i, name, &nameLen, nullptr, &type, data, &dataSize) != ERROR_SUCCESS)
            continue;
        if (type != REG_SZ)
            continue;
        data[dataSize] = 0;
        data[dataSize + 1] = 0;
        const QString valName = QString::fromWCharArray(name, nameLen);
        QString friendly = QString::fromWCharArray((const wchar_t*)data);
        while (friendly.endsWith(QChar(0)))
            friendly.chop(1);

        QString path;
        if (valName.endsWith(".FriendlyAppName"))
            path = valName.left(valName.size() - int(qstrlen(".FriendlyAppName")));
        else if (valName.endsWith(".ApplicationCompany"))
            path = valName.left(valName.size() - int(qstrlen(".ApplicationCompany")));
        else
            continue;
        if (path.startsWith('@') || path.isEmpty())
            continue;
        if (!path.contains('\\'))
            continue;
        if (fileExistsSmart(path))
            continue;
        RegIssue it;
        it.kind = RegIssue::Kind::MuiCacheOrphan;
        it.isKey = false;
        it.hiveName = QString::fromWCharArray(L"HKEY_CURRENT_USER");
        it.subKey = QString::fromWCharArray(sub);
        it.valueName = valName;
        it.title = friendly.isEmpty() ? path : friendly;
        it.detail = QString("路径已不存在：%1").arg(path);
        out->append(it);
    }
    RegCloseKey(k);
}

// ---------- 废弃路径 / 遗留数据 ----------

QSet<QChar> liveDriveLetters()
{
    QSet<QChar> s;
    const DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i)
        if (mask & (1u << i))
            s.insert(QChar(wchar_t(L'A' + i)));
    return s;
}

// 展开 %VAR% 后判断"这个目标是不是没了"。裸文件名交给 SearchPathW（exe/dll 都能查到），
// 不然 system32 里那些只写了文件名的项会被全部误判成失效。
bool targetMissing(const QString& rawIn)
{
    QString p = Platform::expandEnv(rawIn.trimmed());
    p.remove(QLatin1Char('"'));
    if (p.isEmpty())
        return false;                       // 没写过值 ≠ 失效
    if (QFileInfo::exists(p))
        return false;
    if (!p.contains(QLatin1Char('\\')) && !p.contains(QLatin1Char('/'))) {
        wchar_t found[MAX_PATH];
        return SearchPathW(nullptr, (LPCWSTR)p.utf16(), nullptr, MAX_PATH, found, nullptr) == 0;
    }
    return true;
}

// 枚举子键名。cap 是给 COM 那种上万个键的场合兜底的。
QStringList subKeys(HKEY parent, int cap)
{
    QStringList out;
    DWORD vals = 0, maxName = 0;
    RegQueryInfoKeyW(parent, nullptr, nullptr, nullptr, &vals, &maxName, nullptr,
                     nullptr, nullptr, nullptr, nullptr, nullptr);
    std::wstring buf(size_t(maxName) + 2, 0);
    for (DWORD i = 0; i < vals; ++i) {
        DWORD len = DWORD(buf.size());
        if (RegEnumKeyExW(parent, i, buf.data(), &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            continue;
        out << QString::fromWCharArray(buf.data(), int(len));
        if (out.size() >= cap)
            break;
    }
    return out;
}

void addIssue(QVector<RegIssue>* out, RegIssue::Kind kind, RegIssue::Risk risk,
              const QString& hive, const QString& sub, const QString& val, bool isKey,
              const QString& title, const QString& why, const QString& bad)
{
    RegIssue it;
    it.kind = kind;
    it.risk = risk;
    it.hiveName = hive;
    it.subKey = sub;
    it.valueName = val;
    it.isKey = isKey;
    it.title = title;
    it.detail = why;
    it.badPath = bad;
    out->append(it);
}

void scanAppPaths(const HiveKey& hk, QVector<RegIssue>* out)
{
    HKEY k = nullptr;
    if (RegOpenKeyExW(hk.root, hk.sub, 0, KEY_READ, &k) != ERROR_SUCCESS)
        return;
    const QString hive = QString::fromWCharArray(hk.hiveName);
    const QString base = QString::fromWCharArray(hk.sub);
    for (const QString& app : subKeys(k, 4000)) {
        HKEY sk = nullptr;
        if (RegOpenKeyExW(k, (LPCWSTR)app.utf16(), 0, KEY_READ, &sk) != ERROR_SUCCESS)
            continue;
        const QString def = readString(sk, nullptr);
        RegCloseKey(sk);
        if (def.isEmpty())
            continue;
        const QString exe = extractExeFromCommand(def);
        if (underWindowsDir(exe) || !targetMissing(exe))
            continue;
        addIssue(out, RegIssue::Kind::AppPathsOrphan, RegIssue::Risk::Safe, hive,
                 base + "\\" + app, QString(), true, app,
                 QString("按名字启动的目标已不存在：%1").arg(exe), exe);
    }
    RegCloseKey(k);
}

// ProgID 是否存在（HKCU / HKLM / 32 位视图都算）
bool progIdExists(const QString& progId)
{
    if (progId.isEmpty())
        return false;
    const HKEY hs[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    const wchar_t* bases[] = { L"Software\\Classes\\", L"Software\\Classes\\Wow6432Node\\" };
    const std::wstring rel = progId.toStdWString();
    for (HKEY h : hs) {
        for (const wchar_t* b : bases) {
            HKEY k = nullptr;
            const std::wstring p = std::wstring(b) + rel;
            const bool ok = RegOpenKeyExW(h, p.c_str(), 0, KEY_READ, &k) == ERROR_SUCCESS;
            if (ok)
                RegCloseKey(k);
            if (ok)
                return true;
        }
    }
    return false;
}

// 按 HKCR 的合并顺序（HKCU 覆盖 HKLM，再退到 32 位视图）读一个 Classes 下的值
QString readClassesString(const QString& rel, const wchar_t* valueName)
{
    const HKEY hs[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    const wchar_t* bases[] = { L"Software\\Classes\\", L"Software\\Classes\\Wow6432Node\\" };
    const std::wstring r = rel.toStdWString();
    for (HKEY h : hs) {
        for (const wchar_t* b : bases) {
            HKEY k = nullptr;
            const std::wstring p = std::wstring(b) + r;
            if (RegOpenKeyExW(h, p.c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS)
                continue;
            QString v = readString(k, valueName);
            RegCloseKey(k);
            if (!v.isEmpty())
                return v;
        }
    }
    return {};
}

void scanAssociations(QVector<RegIssue>* out)
{
    const struct { HKEY root; const wchar_t* hive; const wchar_t* sub; } roots[] = {
        { HKEY_CURRENT_USER, L"HKEY_CURRENT_USER", L"Software\\Classes" },
        { HKEY_LOCAL_MACHINE, L"HKEY_LOCAL_MACHINE", L"Software\\Classes" },
    };
    QStringList seen;
    for (const auto& r : roots) {
        HKEY k = nullptr;
        if (RegOpenKeyExW(r.root, r.sub, 0, KEY_READ, &k) != ERROR_SUCCESS)
            continue;
        const QString hive = QString::fromWCharArray(r.hive);
        const QString base = QString::fromWCharArray(r.sub);
        for (const QString& ext : subKeys(k, 3000)) {
            if (!ext.startsWith(QLatin1Char('.')))
                continue;
            const QString low = ext.toLower();
            if (seen.contains(low))
                continue;
            seen << low;
            HKEY ek = nullptr;
            if (RegOpenKeyExW(k, (LPCWSTR)ext.utf16(), 0, KEY_READ, &ek) != ERROR_SUCCESS)
                continue;
            const QString progId = readString(ek, nullptr);
            RegCloseKey(ek);
            // AppX\... 是 UWP 注册的方式，Classes 里本来就没有对应键，别当成残留
            if (progId.isEmpty() || progId.startsWith(QLatin1String("AppX"), Qt::CaseInsensitive))
                continue;
            if (!progIdExists(progId)) {
                addIssue(out, RegIssue::Kind::AssociationOrphan, RegIssue::Risk::Careful, hive,
                         base + "\\" + ext, QString(), true, ext,
                         QString("扩展名指向的 ProgID 不存在：%1").arg(progId), progId);
                continue;
            }
            const QString cmd = readClassesString(progId + "\\shell\\open\\command", nullptr);
            if (cmd.isEmpty())
                continue;
            const QString exe = extractExeFromCommand(cmd);
            if (exe.isEmpty() || exe.startsWith(QLatin1Char('@')))
                continue;
            if (!targetMissing(exe))
                continue;
            // 关联本身还在、只是目标程序被删了：删哪个键都不对，只能提示重新关联
            addIssue(out, RegIssue::Kind::AssociationOrphan, RegIssue::Risk::ReadOnly, hive,
                     base + "\\" + ext, QString(), false, ext,
                     QString("打开方式里的程序已不存在：%1（%2）").arg(exe, progId), exe);
        }
        RegCloseKey(k);
    }
}

void scanCom(QVector<RegIssue>* out)
{
    const struct { HKEY root; const wchar_t* hive; const wchar_t* sub; } roots[] = {
        { HKEY_LOCAL_MACHINE, L"HKEY_LOCAL_MACHINE", L"Software\\Classes\\CLSID" },
        { HKEY_LOCAL_MACHINE, L"HKEY_LOCAL_MACHINE", L"Software\\Classes\\Wow6432Node\\CLSID" },
        { HKEY_CURRENT_USER, L"HKEY_CURRENT_USER", L"Software\\Classes\\CLSID" },
    };
    static const wchar_t* kServers[] = { L"InprocServer32", L"LocalServer32" };
    for (const auto& r : roots) {
        HKEY k = nullptr;
        if (RegOpenKeyExW(r.root, r.sub, 0, KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS)
            continue;
        const QString hive = QString::fromWCharArray(r.hive);
        const QString base = QString::fromWCharArray(r.sub);
        for (const QString& clsid : subKeys(k, 25000)) {
            if (!clsid.startsWith(QLatin1Char('{')))
                continue;
            HKEY ck = nullptr;
            if (RegOpenKeyExW(k, (LPCWSTR)clsid.utf16(), 0, KEY_READ, &ck) != ERROR_SUCCESS)
                continue;
            for (const wchar_t* srv : kServers) {
                HKEY sk = nullptr;
                if (RegOpenKeyExW(ck, srv, 0, KEY_READ, &sk) != ERROR_SUCCESS)
                    continue;
                const QString raw = readString(sk, nullptr);
                RegCloseKey(sk);
                // LocalServer32 写的常是一条命令行（"C:\...\a.exe" -service），取里面的 exe
                const QString target = extractExeFromCommand(raw);
                if (target.isEmpty() || underWindowsDir(target) || !targetMissing(target))
                    continue;
                addIssue(out, RegIssue::Kind::ComOrphan, RegIssue::Risk::Careful, hive,
                         base + "\\" + clsid + "\\" + QString::fromWCharArray(srv),
                         QString(), false, QString("%1 (%2)").arg(clsid, QString::fromWCharArray(srv)),
                         QString("组件文件已不存在：%1").arg(target), target);
            }
            RegCloseKey(ck);
        }
        RegCloseKey(k);
    }
}

void scanServices(QVector<RegIssue>* out)
{
    const wchar_t* sub = L"SYSTEM\\CurrentControlSet\\Services";
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, sub, 0, KEY_READ, &k) != ERROR_SUCCESS)
        return;
    for (const QString& name : subKeys(k, 5000)) {
        HKEY sk = nullptr;
        if (RegOpenKeyExW(k, (LPCWSTR)name.utf16(), 0, KEY_READ, &sk) != ERROR_SUCCESS)
            continue;
        const QString img = readString(sk, L"ImagePath");
        RegCloseKey(sk);
        if (img.isEmpty())
            continue;
        QString p = img.trimmed();
        if (p.startsWith(QLatin1String("\\??\\")))
            p = p.mid(4);
        const QString sysRoot = Platform::expandEnv(QStringLiteral("%SystemRoot%"));
        if (p.startsWith(QLatin1String("\\SystemRoot\\"), Qt::CaseInsensitive))
            p = sysRoot + QLatin1Char('\\') + p.mid(12);
        else if (p.startsWith(QLatin1String("\\Windows\\"), Qt::CaseInsensitive))
            p = sysRoot + QLatin1Char('\\') + p.mid(9);
        else if (p.startsWith(QLatin1String("System32"), Qt::CaseInsensitive)
                 || p.startsWith(QLatin1String("SysWow64"), Qt::CaseInsensitive))
            p = sysRoot + QLatin1Char('\\') + p;
        p = Platform::expandEnv(p);
        p.remove(QLatin1Char('"'));
        const QString exe = extractExeFromCommand(p);
        // 只判绝对路径：内核驱动那些裸名/相对名由内核按自己的规则找，
        // 拿 Win32 的 Exists 去判会一口气误报几百条（实测 270 条几乎全是这个）
        if (!exe.contains(QLatin1Char(':')) || underWindowsDir(exe) || !targetMissing(exe))
            continue;
        addIssue(out, RegIssue::Kind::ServiceOrphan, RegIssue::Risk::Careful,
                 QStringLiteral("HKEY_LOCAL_MACHINE"), QString::fromWCharArray(sub) + "\\" + name,
                 QString(), true, name,
                 QString("服务的可执行文件已不存在：%1").arg(exe), exe);
    }
    RegCloseKey(k);
}

void scanDeadDrives(QVector<RegIssue>* out)
{
    const QSet<QChar> live = liveDriveLetters();

    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\MountedDevices", 0, KEY_READ, &k) == ERROR_SUCCESS) {
        DWORD vals = 0, maxName = 0;
        RegQueryInfoKeyW(k, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &vals,
                         &maxName, nullptr, nullptr, nullptr);
        std::wstring buf(size_t(maxName) + 2, 0);
        for (DWORD i = 0; i < vals; ++i) {
            DWORD len = DWORD(buf.size());
            if (RegEnumValueW(k, i, buf.data(), &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                continue;
            const QString name = QString::fromWCharArray(buf.data(), int(len));
            if (!name.startsWith(QLatin1String("\\DosDevices\\")) || !name.endsWith(QLatin1Char(':')))
                continue;
            const QChar letter = name.at(name.size() - 2).toUpper();
            if (letter < QLatin1Char('A') || letter > QLatin1Char('Z') || live.contains(letter))
                continue;
            addIssue(out, RegIssue::Kind::DeadDrive, RegIssue::Risk::Careful,
                     QStringLiteral("HKEY_LOCAL_MACHINE"), QStringLiteral("SYSTEM\\MountedDevices"),
                     name, false, QString("%1: 盘符").arg(letter),
                     QStringLiteral("这个盘已经不在了，挂载记录还留着"),
                     QString("%1:").arg(letter));
        }
        RegCloseKey(k);
    }

    const wchar_t* mp = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\MountPoints2";
    if (RegOpenKeyExW(HKEY_CURRENT_USER, mp, 0, KEY_READ, &k) == ERROR_SUCCESS) {
        for (const QString& name : subKeys(k, 500)) {
            // 形如 "D_" / "d" 的子键是盘符挂载点；"{guid}" 那种没法直接判断，跳过
            if (name.size() < 2 || !name.at(0).isLetter() || name.at(1) != QLatin1Char('_'))
                continue;
            const QChar letter = name.at(0).toUpper();
            if (letter < QLatin1Char('A') || letter > QLatin1Char('Z') || live.contains(letter))
                continue;
            addIssue(out, RegIssue::Kind::DeadDrive, RegIssue::Risk::Safe,
                     QStringLiteral("HKEY_CURRENT_USER"),
                     QString::fromWCharArray(mp) + "\\" + name, QString(), true,
                     QString("%1: 挂载点记录").arg(letter),
                     QStringLiteral("这个盘已经不在了，挂载点记录还留着"),
                     QString("%1:").arg(letter));
        }
        RegCloseKey(k);
    }
}

void scanPathEntries(QVector<RegIssue>* out)
{
    const struct { HKEY root; const wchar_t* hive; const wchar_t* sub; const wchar_t* val;
                   const char* label; } targets[] = {
        { HKEY_CURRENT_USER, L"HKEY_CURRENT_USER", L"Environment", L"Path", "用户 PATH" },
        { HKEY_LOCAL_MACHINE, L"HKEY_LOCAL_MACHINE",
          L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", L"Path", "系统 PATH" },
    };
    for (const auto& t : targets) {
        HKEY k = nullptr;
        if (RegOpenKeyExW(t.root, t.sub, 0, KEY_READ, &k) != ERROR_SUCCESS)
            continue;
        const QString raw = readString(k, t.val);
        RegCloseKey(k);
        for (const QString& piece : raw.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            const QString p = Platform::expandEnv(piece.trimmed());
            if (p.isEmpty() || !p.contains(QLatin1Char(':')) || QFileInfo::exists(p))
                continue;
            // 整条 Path 值不能删，只能告诉用户哪一段该去掉
            addIssue(out, RegIssue::Kind::PathEntry, RegIssue::Risk::ReadOnly,
                     QString::fromWCharArray(t.hive), QString::fromWCharArray(t.sub),
                     QString::fromWCharArray(t.val), false,
                     QString::fromUtf8(t.label),
                     QString("目录已不存在：%1").arg(p), p);
        }
    }
}

void scanLegacyKeys(QVector<RegIssue>* out)
{
    const struct { HKEY root; const wchar_t* hive; const wchar_t* sub; const char* why; } list[] = {
        { HKEY_LOCAL_MACHINE, L"HKEY_LOCAL_MACHINE",
          L"Software\\Microsoft\\Windows\\CurrentVersion\\RunServices",
          "Windows 9x 的启动键，NT 内核从来不读它" },
        { HKEY_CURRENT_USER, L"HKEY_CURRENT_USER",
          L"Software\\Microsoft\\Windows\\CurrentVersion\\RunServices",
          "同上：用户级的 9x 遗留启动键" },
        { HKEY_LOCAL_MACHINE, L"HKEY_LOCAL_MACHINE",
          L"Software\\Microsoft\\Windows\\CurrentVersion\\SharedDLLs",
          "9x 时代的 DLL 引用计数，现在的安装程序早就不维护它了" },
    };
    for (const auto& l : list) {
        HKEY k = nullptr;
        if (RegOpenKeyExW(l.root, l.sub, 0, KEY_READ, &k) != ERROR_SUCCESS)
            continue;
        RegCloseKey(k);
        const QString sub = QString::fromWCharArray(l.sub);
        addIssue(out, RegIssue::Kind::LegacyKey, RegIssue::Risk::Safe,
                 QString::fromWCharArray(l.hive), sub, QString(), true,
                 sub.section('\\', -1), QString::fromUtf8(l.why), QString());
    }
}

// ---------- .reg 备份导出 ----------

QString hexBytes(const BYTE* d, DWORD n)
{
    QStringList parts;
    for (DWORD i = 0; i < n; ++i)
        parts.append(QString::asprintf("%02x", d[i]));
    return parts.join(',');
}

QString regLine(const QString& valueName, DWORD type, const BYTE* data, DWORD size)
{
    QString name = valueName.isEmpty() ? "@" : "\"" + valueName + "\"";
    name.replace('"', "\\\"");
    switch (type) {
    case REG_SZ: {
        QString s = QString::fromWCharArray((const wchar_t*)data, size / 2);
        while (s.endsWith(QChar(0)))
            s.chop(1);
        s.replace('\\', "\\\\");
        s.replace('"', "\\\"");
        s.replace('\r', " ");
        s.replace('\n', " ");
        return name + "=\"" + s + "\"";
    }
    case REG_EXPAND_SZ:
        return name + "=hex(2):" + hexBytes(data, size);
    case REG_BINARY:
        return name + "=hex:" + hexBytes(data, size);
    case REG_DWORD: {
        if (size < 4)
            return {};
        DWORD v = 0;
        memcpy(&v, data, 4);
        return name + "=dword:" + QString::asprintf("%08x", v);
    }
    case REG_QWORD:
        return name + "=hex(b):" + hexBytes(data, size);
    default:
        return {};
    }
}

// 删除前把对应键/值导出为 .reg 片段
QString exportFragment(HKEY root, const RegIssue& it)
{
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, (LPCWSTR)it.subKey.utf16(), 0, KEY_READ, &k) != ERROR_SUCCESS)
        return {};
    QString out = QString("[%1\\%2]\r\n").arg(it.hiveName, it.subKey);
    if (it.isKey) {
        DWORD vals = 0, maxName = 0, maxData = 0;
        RegQueryInfoKeyW(k, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &vals,
                         &maxName, &maxData, nullptr, nullptr);
        std::wstring nameBuf(size_t(maxName) + 1, 0);
        QByteArray dataBuf(int(maxData) + 8, 0);
        for (DWORD i = 0; i < vals; ++i) {
            DWORD nameLen = DWORD(nameBuf.size());
            DWORD type = 0, dataSize = DWORD(dataBuf.size());
            if (RegEnumValueW(k, i, nameBuf.data(), &nameLen, nullptr, &type,
                              (BYTE*)dataBuf.data(), &dataSize) != ERROR_SUCCESS)
                continue;
            const QString line = regLine(QString::fromWCharArray(nameBuf.data(), nameLen),
                                         type, (const BYTE*)dataBuf.constData(), dataSize);
            if (!line.isEmpty())
                out += line + "\r\n";
        }
    } else {
        DWORD type = 0, size = 0;
        if (RegQueryValueExW(k, (LPCWSTR)it.valueName.utf16(), nullptr, &type, nullptr, &size)
            == ERROR_SUCCESS) {
            QByteArray buf(int(size), 0);
            if (RegQueryValueExW(k, (LPCWSTR)it.valueName.utf16(), nullptr, &type,
                                 (BYTE*)buf.data(), &size) == ERROR_SUCCESS) {
                const QString line = regLine(it.valueName, type, (const BYTE*)buf.constData(), size);
                if (!line.isEmpty())
                    out += line + "\r\n";
            }
        }
    }
    RegCloseKey(k);
    return out;
}

QString appendBackup(const QString& fragment, QString* backupPath)
{
    QDir().mkpath(Utils::backupsDir());
    const QString f = QString("%1/registry-backup-%2.reg")
                          .arg(Utils::backupsDir(), QDate::currentDate().toString("yyyyMMdd"));
    const bool newFile = !QFileInfo::exists(f);
    QFile file(f);
    if (!file.open(QIODevice::Append | QIODevice::Text))
        return {};
    QTextStream ts(&file);
    ts.setEncoding(QStringConverter::Utf16LE);
    ts.setGenerateByteOrderMark(true);
    if (newFile)
        ts << "Windows Registry Editor Version 5.00\r\n\r\n";
    ts << fragment << "\r\n";
    ts.flush();
    if (backupPath)
        *backupPath = f;
    return f;
}

} // namespace

namespace RegClean {

QVector<RegIssue> quickScan()
{
    QVector<RegIssue> out;
    out.reserve(64);

    const wchar_t* unHKLM = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
    const wchar_t* unWOW = L"Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
    const wchar_t* runHKLM = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t* runHKCU = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t* runOnceHKLM = L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce";
    const wchar_t* runOnceHKCU = L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce";
    const wchar_t* appHKLM = L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths";
    const wchar_t* appWOW = L"Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\App Paths";

    scanUninstall({HKEY_LOCAL_MACHINE, L"HKEY_LOCAL_MACHINE", unHKLM}, &out);
    scanUninstall({HKEY_LOCAL_MACHINE, L"HKEY_LOCAL_MACHINE", unWOW}, &out);
    scanUninstall({HKEY_CURRENT_USER, L"HKEY_CURRENT_USER", unHKLM}, &out);
    scanRun({HKEY_LOCAL_MACHINE, L"HKEY_LOCAL_MACHINE", runHKLM}, &out);
    scanRun({HKEY_CURRENT_USER, L"HKEY_CURRENT_USER", runHKCU}, &out);
    scanRun({HKEY_LOCAL_MACHINE, L"HKEY_LOCAL_MACHINE", runOnceHKLM}, &out);
    scanRun({HKEY_CURRENT_USER, L"HKEY_CURRENT_USER", runOnceHKCU}, &out);
    scanMuiCache(&out);
    scanAppPaths({HKEY_LOCAL_MACHINE, L"HKEY_LOCAL_MACHINE", appHKLM}, &out);
    scanAppPaths({HKEY_LOCAL_MACHINE, L"HKEY_LOCAL_MACHINE", appWOW}, &out);
    scanAssociations(&out);
    scanServices(&out);
    scanDeadDrives(&out);
    scanPathEntries(&out);
    scanLegacyKeys(&out);

    return out;
}

QVector<RegIssue> scan()
{
    QVector<RegIssue> out = quickScan();
    scanCom(&out);          // 上万个 CLSID 键，唯一慢的一类：只在分析页里跑
    return out;
}

bool deleteIssue(const RegIssue& it, QString* err)
{
    HKEY root = it.hiveName == "HKEY_LOCAL_MACHINE" ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;

    QString backupPath;
    const QString frag = exportFragment(root, it);
    if (!frag.isEmpty())
        appendBackup(frag, &backupPath);

    LSTATUS st;
    if (it.isKey) {
        st = RegDeleteTreeW(root, (LPCWSTR)it.subKey.utf16());
    } else {
        // RegDeleteValueW 需要先打开包含该值的键
        HKEY k = nullptr;
        st = RegOpenKeyExW(root, (LPCWSTR)it.subKey.utf16(), 0, KEY_SET_VALUE, &k);
        if (st == ERROR_SUCCESS) {
            st = RegDeleteValueW(k, (LPCWSTR)it.valueName.utf16());
            RegCloseKey(k);
        }
    }
    if (st != ERROR_SUCCESS) {
        if (err) {
            *err = QString("删除失败 (错误 %1)%2").arg(st).arg(
                st == ERROR_ACCESS_DENIED ? "：需要管理员权限" : "");
        }
        return false;
    }
    return true;
}

QString categoryLabel(const RegIssue& it)
{
    switch (it.kind) {
    case RegIssue::Kind::UninstallOrphan:   return "卸载残留";
    case RegIssue::Kind::RunOrphan:         return "启动项失效";
    case RegIssue::Kind::MuiCacheOrphan:    return "缓存残留";
    case RegIssue::Kind::AppPathsOrphan:    return "App Paths 失效";
    case RegIssue::Kind::AssociationOrphan: return "文件关联";
    case RegIssue::Kind::ComOrphan:         return "COM 组件";
    case RegIssue::Kind::ServiceOrphan:     return "服务残留";
    case RegIssue::Kind::DeadDrive:         return "死盘符";
    case RegIssue::Kind::PathEntry:         return "PATH 失效项";
    case RegIssue::Kind::LegacyKey:         return "9x 遗留键";
    }
    return {};
}

QString riskLabel(const RegIssue& it)
{
    switch (it.risk) {
    case RegIssue::Risk::Safe:     return "可安全删除";
    case RegIssue::Risk::Careful:  return "可删除，但会改变某项行为";
    case RegIssue::Risk::ReadOnly: return "只提示，不能整值删除";
    }
    return {};
}

} // namespace RegClean
