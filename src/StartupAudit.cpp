#include "StartupAudit.h"
#include "platform/Paths.h"

#include "Utils.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QStringList>

#include <windows.h>
#include <initguid.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <knownfolders.h>
#include <taskschd.h>
#include <softpub.h>
#include <wintrust.h>
#include <wincrypt.h>

namespace {

// ---------- 通用小件 ----------

QString wideToQt(const wchar_t* s)
{
    return s ? QString::fromWCharArray(s) : QString();
}

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
    QString s = QString::fromWCharArray((const wchar_t*)buf.data(), int(size) / 2);
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

QStringList childKeys(HKEY parent, int cap)
{
    QStringList out;
    DWORD count = 0, maxName = 0;
    RegQueryInfoKeyW(parent, nullptr, nullptr, nullptr, &count, &maxName, nullptr,
                     nullptr, nullptr, nullptr, nullptr, nullptr);
    std::wstring buf(size_t(maxName) + 2, 0);
    for (DWORD i = 0; i < count; ++i) {
        DWORD len = DWORD(buf.size());
        if (RegEnumKeyExW(parent, i, buf.data(), &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            continue;
        out << QString::fromWCharArray(buf.data(), int(len));
        if (out.size() >= cap)
            break;
    }
    return out;
}

// 任务管理器「启动」页里被用户关掉的那些，状态存在 StartupApproved 的一个 12 字节 blob 里：
// 首字节 0x03 = 启用，其它（0x02/0x82…）= 已禁用。没有这个值就按启用算。
bool approvedFlag(HKEY root, const QString& approvedSub, const QString& valueName)
{
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, (LPCWSTR)approvedSub.utf16(), 0, KEY_READ, &k) != ERROR_SUCCESS)
        return true;
    bool enabled = true;
    BYTE blob[16] = {};
    DWORD size = sizeof(blob), type = 0;
    if (RegQueryValueExW(k, (LPCWSTR)valueName.utf16(), nullptr, &type, blob, &size) == ERROR_SUCCESS
        && size >= 1)
        enabled = (blob[0] == 0x03);
    RegCloseKey(k);
    return enabled;
}

// ---------- 数字签名（纯本地读，不联网做吊销检查） ----------

struct SigInfo {
    bool checked = false;
    bool has = false;
    bool valid = false;
    QString publisher;
};

QHash<QString, SigInfo>& sigCache()
{
    static QHash<QString, SigInfo> c;
    return c;
}

SigInfo querySignature(const QString& rawPath)
{
    SigInfo info;
    info.checked = true;
    const QString path = QDir::toNativeSeparators(Platform::expandEnv(rawPath));
    if (path.isEmpty() || !QFileInfo::exists(path))
        return info;
    const QString key = path.toLower();
    if (sigCache().contains(key))
        return sigCache().value(key);

    std::wstring w = path.toStdWString();

    HCERTSTORE store = nullptr;
    HCRYPTMSG msg = nullptr;
    DWORD encoding = 0, contentType = 0, formatType = 0;
    if (CryptQueryObject(CERT_QUERY_OBJECT_FILE, w.c_str(),
                         CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED, CERT_QUERY_FORMAT_FLAG_BINARY,
                         0, &encoding, &contentType, &formatType, &store, &msg, nullptr)) {
        // 签名者不能靠"证书库里第一个"来定：那里常常是时间戳 CA。
        // 正确做法是从 PKCS#7 里取 signer 的 Issuer+SerialNumber，再回库里找那张证书。
        DWORD signerLen = 0;
        if (msg && CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, nullptr, &signerLen)) {
            QByteArray buf(int(signerLen), 0);
            if (CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, (BYTE*)buf.data(), &signerLen)) {
                auto* si = reinterpret_cast<CMSG_SIGNER_INFO*>(buf.data());
                CERT_INFO want{};
                want.Issuer = si->Issuer;
                want.SerialNumber = si->SerialNumber;
                PCCERT_CONTEXT cert = CertFindCertificateInStore(
                    store, encoding, 0, CERT_FIND_SUBJECT_CERT, &want, nullptr);
                if (cert) {
                    info.has = true;
                    wchar_t name[256] = {};
                    if (CertGetNameStringW(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr,
                                           name, 256) > 1)
                        info.publisher = wideToQt(name);
                    CertFreeCertificateContext(cert);
                }
            }
        }
        if (msg)
            CryptMsgClose(msg);
        CertCloseStore(store, 0);
    }

    if (info.has) {
        // wintrust.h 里 WINTRUST_ACTION_GENERIC_VERIFY_V2 的定义方式跟 initguid 的包含顺序
        // 有关，本地写一份常量最省事（值就是那个 GUID，不会变）
        // 这个在 softpub.h 里是个"花括号初始化宏"而不是 GUID 常量（所以 &宏名 编译不过），
        // 手抄数字容易错一位就变 TRUST_E_PROVIDER_UNKNOWN，直接展开宏最稳
        static const GUID kVerifyV2 = WINTRUST_ACTION_GENERIC_VERIFY_V2;
        WINTRUST_FILE_INFO fi{};
        fi.cbStruct = sizeof(fi);
        fi.pcwszFilePath = w.c_str();
        WINTRUST_DATA wd{};
        wd.cbStruct = sizeof(wd);
        wd.dwUIChoice = WTD_UI_NONE;
        wd.fdwRevocationChecks = WTD_REVOKE_NONE;      // 别为了判签名去联网
        wd.dwUnionChoice = WTD_CHOICE_FILE;
        wd.pFile = &fi;
        wd.dwStateAction = WTD_STATEACTION_VERIFY;
        // 不查吊销 + 只允许用本地缓存：否则每个文件都可能去联网取 CRL/AIA，
        // 实测 99 项要 73 秒；加上这两个标志后是亚秒级
        wd.dwProvFlags = WTD_DISABLE_MD2_MD4 | WTD_CACHE_ONLY_URL_RETRIEVAL;
        const LONG vst = WinVerifyTrust(nullptr, const_cast<GUID*>(&kVerifyV2), &wd);
        info.valid = vst == ERROR_SUCCESS;
        wd.dwStateAction = WTD_STATEACTION_CLOSE;
        WinVerifyTrust(nullptr, const_cast<GUID*>(&kVerifyV2), &wd);
    }
    sigCache().insert(key, info);
    return info;
}

void finish(StartupItem& it)
{
    if (it.targetPath.isEmpty())
        return;
    it.targetMissing = Utils::fileTargetMissing(it.targetPath);
    const SigInfo sig = querySignature(it.targetPath);
    it.checkedSig = sig.checked && !it.targetMissing;
    it.hasSignature = sig.has;
    it.signatureValid = sig.valid;
    it.publisher = sig.publisher;
}

// ---------- 注册表 Run / RunOnce ----------

void scanRunKeys(QVector<StartupItem>* out)
{
    struct Tgt {
        HKEY root;
        const wchar_t* hive;
        const wchar_t* runSub;
        const wchar_t* approvedSub;   // 空 = 用 Run 同级的 StartupApproved\Run
        const char* label;
    };
    const wchar_t* cuRun = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t* cuRunOnce = L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce";
    const wchar_t* cuPolicy = L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer\\Run";
    const wchar_t* lmBase = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
    const wchar_t* lmWowRun = L"Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run";

    const Tgt tgts[] = {
        { HKEY_CURRENT_USER, L"HKEY_CURRENT_USER", cuRun, lmBase, "HKCU Run" },
        { HKEY_CURRENT_USER, L"HKEY_CURRENT_USER", cuRunOnce, lmBase, "HKCU RunOnce" },
        { HKEY_CURRENT_USER, L"HKEY_CURRENT_USER", cuPolicy, lmBase, "HKCU 策略 Run" },
        { HKEY_LOCAL_MACHINE, L"HKEY_LOCAL_MACHINE", cuRun, lmBase, "HKLM Run" },
        { HKEY_LOCAL_MACHINE, L"HKEY_LOCAL_MACHINE", cuRunOnce, lmBase, "HKLM RunOnce" },
        { HKEY_LOCAL_MACHINE, L"HKEY_LOCAL_MACHINE", lmWowRun,
          L"Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run",
          "HKLM Run (32 位)" },
    };
    for (const Tgt& t : tgts) {
        HKEY k = nullptr;
        if (RegOpenKeyExW(t.root, t.runSub, 0, KEY_READ, &k) != ERROR_SUCCESS)
            continue;
        DWORD vals = 0, maxName = 0, maxData = 0;
        RegQueryInfoKeyW(k, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &vals,
                         &maxName, &maxData, nullptr, nullptr);
        std::wstring nm(size_t(maxName) + 2, 0);
        QByteArray data(int(maxData) + 8, 0);
        for (DWORD i = 0; i < vals; ++i) {
            DWORD nameLen = DWORD(nm.size()), type = 0, size = DWORD(data.size());
            if (RegEnumValueW(k, i, nm.data(), &nameLen, nullptr, &type,
                              (BYTE*)data.data(), &size) != ERROR_SUCCESS)
                continue;
            if (type != REG_SZ && type != REG_EXPAND_SZ)
                continue;
            StartupItem it;
            it.source = StartupItem::Source::RegistryRun;
            it.name = QString::fromWCharArray(nm.data(), int(nameLen));
            it.command = QString::fromWCharArray((const wchar_t*)data.constData(), int(size) / 2)
                             .trimmed();
            while (it.command.endsWith(QChar(0)))
                it.command.chop(1);
            if (it.name.isEmpty() || it.command.isEmpty())
                continue;
            it.location = QString::fromWCharArray(t.hive) + "\\" + QString::fromWCharArray(t.runSub);
            it.note = t.label;
            it.targetPath = Utils::exeFromCommand(it.command);
            it.enabled = approvedFlag(t.root, QString::fromWCharArray(t.approvedSub), it.name);
            finish(it);
            out->append(it);
        }
        RegCloseKey(k);
    }
}

// ---------- 启动文件夹（.lnk 解析到真实目标） ----------

QString lnkTarget(const QString& lnkPath)
{
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IShellLinkW, reinterpret_cast<void**>(&link))))
        return {};
    IPersistFile* pf = nullptr;
    QString target;
    if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&pf)))) {
        if (SUCCEEDED(pf->Load((LPCWSTR)lnkPath.utf16(), STGM_READ | STGM_SHARE_EXCLUSIVE)))
            link->Resolve(nullptr, SLR_NO_UI | SLR_NOTRACK | SLR_UPDATE);
        wchar_t buf[MAX_PATH] = {};
        if (SUCCEEDED(link->GetPath(buf, MAX_PATH, nullptr, SLGP_RAWPATH)))
            target = wideToQt(buf);
        pf->Release();
    }
    link->Release();
    return target;
}

void scanStartupFolders(QVector<StartupItem>* out)
{
    const struct { REFKNOWNFOLDERID id; const char* label; } folders[] = {
        { FOLDERID_Startup, "用户启动文件夹" },
        { FOLDERID_CommonStartup, "全体用户启动文件夹" },
    };
    for (const auto& f : folders) {
        wchar_t* path = nullptr;
        if (FAILED(SHGetKnownFolderPath(f.id, 0, nullptr, &path)))
            continue;
        const QString dir = wideToQt(path);
        CoTaskMemFree(path);
        QDir d(dir);
        for (const QString& name : d.entryList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString full = d.absoluteFilePath(name);
            StartupItem it;
            it.source = StartupItem::Source::StartupFolder;
            it.name = QFileInfo(name).completeBaseName();
            it.command = full;
            it.location = dir;
            it.note = f.label;
            if (name.endsWith(QLatin1String(".lnk"), Qt::CaseInsensitive)) {
                const QString t = lnkTarget(full);
                it.targetPath = t.isEmpty() ? full : t;
            } else {
                it.targetPath = full;
            }
            it.enabled = approvedFlag(HKEY_CURRENT_USER,
                                      QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion"
                                                     "\\Explorer\\StartupApproved\\StartupFolder"),
                                      name);
            finish(it);
            out->append(it);
        }
    }
}

// ---------- 自动启动的服务 ----------

void scanServices(QVector<StartupItem>* out)
{
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services", 0,
                      KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS)
        return;
    const QString sysRoot = Platform::expandEnv(QStringLiteral("%SystemRoot%"));
    for (const QString& name : childKeys(k, 6000)) {
        HKEY sk = nullptr;
        if (RegOpenKeyExW(k, (LPCWSTR)name.utf16(), 0, KEY_READ, &sk) != ERROR_SUCCESS)
            continue;
        const DWORD start = readDword(sk, L"Start", 3);
        const DWORD type = readDword(sk, L"Type", 1);
        if (start != 2 || !(type & 0x10)) {          // 只要"自动"启动的 Win32 服务
            RegCloseKey(sk);
            continue;
        }
        const QString img = readString(sk, L"ImagePath");
        const QString disp = readString(sk, L"DisplayName");
        const DWORD delayed = readDword(sk, L"DelayedAutostart", 0);
        RegCloseKey(sk);

        StartupItem it;
        it.source = StartupItem::Source::Service;
        it.name = disp.isEmpty() ? name : disp;
        it.command = img;
        it.location = QStringLiteral("HKLM\\SYSTEM\\CurrentControlSet\\Services\\") + name;
        it.note = delayed ? QStringLiteral("自动（延迟启动）") : QStringLiteral("自动");
        QString p = img.trimmed();
        if (p.startsWith(QLatin1String("\\??\\")))
            p = p.mid(4);
        if (p.startsWith(QLatin1String("\\SystemRoot\\"), Qt::CaseInsensitive))
            p = sysRoot + QLatin1Char('\\') + p.mid(12);
        else if (p.startsWith(QLatin1String("\\Windows\\"), Qt::CaseInsensitive))
            p = sysRoot + QLatin1Char('\\') + p.mid(9);
        else if (p.startsWith(QLatin1String("System32"), Qt::CaseInsensitive)
                 || p.startsWith(QLatin1String("SysWow64"), Qt::CaseInsensitive))
            p = sysRoot + QLatin1Char('\\') + p;
        it.targetPath = Utils::exeFromCommand(Platform::expandEnv(p));
        it.enabled = true;
        // 只写了裸文件名的服务（svchost 之类）不做存在性判断：内核和 SCM 的找法不一样
        if (it.targetPath.contains(QLatin1Char(':')))
            finish(it);
        else
            it.note += QStringLiteral(" · 路径未绝对化，跳过存在性检查");
        out->append(it);
    }
    RegCloseKey(k);
}

// ---------- 挂在进程里的加载项 ----------

void scanShellHooks(QVector<StartupItem>* out)
{
    const wchar_t* bho = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Browser Helper Objects";
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, bho, 0, KEY_READ | KEY_WOW64_64KEY, &k) == ERROR_SUCCESS) {
        for (const QString& clsid : childKeys(k, 200)) {
            StartupItem it;
            it.source = StartupItem::Source::ShellExtension;
            it.name = QString("浏览器加载项 %1").arg(clsid);
            it.location = QStringLiteral("HKLM\\") + QString::fromWCharArray(bho) + "\\" + clsid;
            it.note = QStringLiteral("IE/旧版 BHO，现代浏览器已不加载");
            it.enabled = true;
            out->append(it);
        }
        RegCloseKey(k);
    }

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Windows",
                      0, KEY_READ | KEY_WOW64_64KEY, &k) == ERROR_SUCCESS) {
        const QString dlls = readString(k, L"AppInit_DLLs");
        const DWORD load = readDword(k, L"LoadAppInit_DLLs", 0);
        RegCloseKey(k);
        for (const QString& one : dlls.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            StartupItem it;
            it.source = StartupItem::Source::ShellExtension;
            it.name = QFileInfo(one).fileName();
            it.command = one;
            it.targetPath = one;
            it.location = QStringLiteral("HKLM\\Software\\Microsoft\\Windows NT\\CurrentVersion\\Windows @AppInit_DLLs");
            it.note = load ? QStringLiteral("每个加载用户32位程序的进程都会注入")
                           : QStringLiteral("已被 LoadAppInit_DLLs=0 关掉");
            it.enabled = load != 0;
            finish(it);
            out->append(it);
        }
    }
}

// ---------- 计划任务（触发器是"启动时 / 登录时"的那些） ----------

QString xmlTag(const QString& xml, const QString& tag)
{
    const QString open = QLatin1Char('<') + tag + QLatin1Char('>');
    const int a = xml.indexOf(open);
    if (a < 0)
        return {};
    const int b = xml.indexOf(QLatin1Char('<'), a + open.size());
    return b < 0 ? QString() : xml.mid(a + open.size(), b - a - int(open.size()));
}

void walkTaskFolder(ITaskFolder* folder, QVector<StartupItem>* out, int depth,
                    int* seenTasks, int* failedFolders)
{
    if (!folder || depth > 6)
        return;

    ITaskFolderCollection* subs = nullptr;
    if (SUCCEEDED(folder->GetFolders(0, &subs))) {
        LONG n = 0;
        subs->get_Count(&n);
        for (LONG i = 1; i <= n; ++i) {
            VARIANT idx;
            VariantInit(&idx);
            idx.vt = VT_I4;
            idx.lVal = i;                       // 这套集合是 1 基下标
            ITaskFolder* sub = nullptr;
            if (SUCCEEDED(subs->get_Item(idx, &sub)) && sub) {
                walkTaskFolder(sub, out, depth + 1, seenTasks, failedFolders);
                sub->Release();
            }
        }
        subs->Release();
    } else {
        ++*failedFolders;
    }

    IRegisteredTaskCollection* tasks = nullptr;
    if (FAILED(folder->GetTasks(TASK_ENUM_HIDDEN, &tasks))) {
        ++*failedFolders;
        return;
    }
    LONG count = 0;
    tasks->get_Count(&count);
    *seenTasks += int(count);
    for (LONG i = 1; i <= count; ++i) {
        VARIANT idx;
        VariantInit(&idx);
        idx.vt = VT_I4;
        idx.lVal = i;
        IRegisteredTask* task = nullptr;
        if (FAILED(tasks->get_Item(idx, &task)) || !task)
            continue;
        VARIANT_BOOL on = VARIANT_TRUE;
        BSTR pathBstr = nullptr, xmlBstr = nullptr;
        task->get_Enabled(&on);
        task->get_Path(&pathBstr);
        task->get_Xml(&xmlBstr);              // 注意：IRegisteredTask 上这个属性叫 Xml 不叫 XmlText
        const QString xml = wideToQt(xmlBstr);
        const QString path = wideToQt(pathBstr);
        const bool boot = xml.contains(QLatin1String("BootTrigger"));
        const bool logon = xml.contains(QLatin1String("LogonTrigger"));
        if (boot || logon) {
            StartupItem it;
            it.source = StartupItem::Source::Task;
            it.name = path.section(QLatin1Char('\\'), -1);
            it.location = QStringLiteral("任务计划程序 ") + path;
            it.note = boot && logon ? QStringLiteral("触发器：启动时 + 登录时")
                                    : (boot ? QStringLiteral("触发器：启动时")
                                            : QStringLiteral("触发器：登录时"));
            it.command = xmlTag(xml, QStringLiteral("Command"));
            const QString args = xmlTag(xml, QStringLiteral("Arguments"));
            it.targetPath = Utils::exeFromCommand(it.command);
            if (it.targetPath.contains(QLatin1String("%SystemRoot%"), Qt::CaseInsensitive)
                || it.targetPath.contains(QLatin1Char('\\')))
                finish(it);
            else
                it.targetPath.clear();
            if (!args.isEmpty())
                it.command += QLatin1Char(' ') + args;
            it.enabled = on == VARIANT_TRUE;
            out->append(it);
        }
        if (pathBstr)
            SysFreeString(pathBstr);
        if (xmlBstr)
            SysFreeString(xmlBstr);
        task->Release();
    }
    tasks->Release();
}

void scanTasks(QVector<StartupItem>* out)
{
    // 每一步失败都要在结果里说一句，不然用户看到的是"这台机器没有计划任务启动项"，
    // 而真相是"我没读到"
    auto complain = [out](const char* step, HRESULT hr) {
        StartupItem it;
        it.source = StartupItem::Source::Task;
        it.name = QStringLiteral("计划任务这一类没读到");
        it.note = QString::fromUtf8(step) + QStringLiteral(" 失败，HRESULT 0x")
                  + QString::asprintf("%08x", quint32(hr));
        it.enabled = true;
        out->append(it);
    };
    const HRESULT ci = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // 这个线程可能已经被 Qt 初始化过（S_FALSE / RPC_E_CHANGED_MODE 都能继续用 COM）
    // CLSID 别按文档写死：文档里那个 BD956DCC-... 是 1.0 时代的，本机 HKCR\Schedule.Service
    // 注册的是 {0F87369F-...}，写死会拿到 REGDB_E_CLASSNOTREG。走 ProgID 让系统自己解析。
    GUID clsid{};
    HRESULT hr = CLSIDFromProgID(L"Schedule.Service", &clsid);
    if (FAILED(hr)) {
        complain("CLSIDFromProgID(Schedule.Service)", hr);
        if (ci == S_OK)
            CoUninitialize();
        return;
    }
    ITaskService* svc = nullptr;
    hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER,
                          __uuidof(ITaskService), reinterpret_cast<void**>(&svc));
    if (FAILED(hr) || !svc) {
        complain("CoCreateInstance(TaskScheduler)", hr);
        if (ci == S_OK)
            CoUninitialize();
        return;
    }
    VARIANT none;
    VariantInit(&none);                       // VT_EMPTY = 本机；传空字符串会被当成"连一台叫空名的机器"
    hr = svc->Connect(none, none, none, none);
    if (FAILED(hr)) {
        complain("ITaskService::Connect", hr);
        svc->Release();
        if (ci == S_OK)
            CoUninitialize();
        return;
    }
    BSTR rootPath = SysAllocString(L"\\");
    ITaskFolder* root = nullptr;
    hr = svc->GetFolder(rootPath, &root);
    SysFreeString(rootPath);
    if (FAILED(hr) || !root) {
        complain("ITaskService::GetFolder(\\\\)", hr);
    } else {
        int seen = 0, failedFolders = 0;
        walkTaskFolder(root, out, 0, &seen, &failedFolders);
        root->Release();
        if (seen == 0)
            complain("枚举任务", failedFolders ? HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED) : S_OK);
        else if (failedFolders > 0) {
            StartupItem it;
            it.source = StartupItem::Source::Task;
            it.name = QStringLiteral("计划任务");
            it.note = QString("共读到 %1 个任务，另有 %2 个文件夹没权限打开")
                          .arg(seen).arg(failedFolders);
            it.enabled = true;
            out->append(it);
        }
    }
    svc->Release();
    if (ci == S_OK)
        CoUninitialize();
}

} // namespace

namespace StartupAudit {

QVector<StartupItem> scan()
{
    QVector<StartupItem> out;
    out.reserve(128);
    scanRunKeys(&out);
    scanStartupFolders(&out);
    scanServices(&out);
    scanShellHooks(&out);
    scanTasks(&out);
    return out;
}

QString sourceLabel(StartupItem::Source s)
{
    switch (s) {
    case StartupItem::Source::RegistryRun:    return QStringLiteral("注册表 Run");
    case StartupItem::Source::StartupFolder:  return QStringLiteral("启动文件夹");
    case StartupItem::Source::Service:        return QStringLiteral("自动启动服务");
    case StartupItem::Source::Task:           return QStringLiteral("计划任务");
    case StartupItem::Source::ShellExtension: return QStringLiteral("进程内加载项");
    }
    return {};
}

} // namespace StartupAudit
