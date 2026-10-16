#include "Firewall.h"
#include "platform/Paths.h"

#include "Utils.h"

#include <QDate>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QThread>

#include <windows.h>
#include <shellapi.h>
#include <netfw.h>

namespace {

// put_* 会自己复制字符串，所以我们分配完立刻释放，别漏
struct Bstr {
    BSTR b = nullptr;
    explicit Bstr(const QString& s) { b = SysAllocString(s.toStdWString().c_str()); }
    ~Bstr() { if (b) SysFreeString(b); }
    Bstr(const Bstr&) = delete;
    Bstr& operator=(const Bstr&) = delete;
    operator BSTR() const { return b; }   // NOLINT：就是想要隐式转换
};

// COM 初始化：GUI 线程 Qt 已经起过（STA），工作线程这里自己起一次。
// RPC_E_CHANGED_MODE 表示已经起过、模式不同，照样能用。
struct ComGuard {
    bool owned = false;
    ComGuard() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        owned = hr == S_OK;
    }
    ~ComGuard() { if (owned) CoUninitialize(); }
};

QString normPath(const QString& raw)
{
    QString p = Platform::expandEnv(raw);
    p.remove(QLatin1Char('"'));
    return QDir::toNativeSeparators(p).toLower();
}

QString bstrToQt(BSTR b)
{
    QString s = b ? QString::fromWCharArray(b) : QString();
    while (s.endsWith(QChar(0)))
        s.chop(1);
    return s.trimmed();
}

bool fromProgID(const wchar_t* progid, GUID* out)
{
    return SUCCEEDED(CLSIDFromProgID(progid, out));
}

INetFwPolicy2* openPolicy()
{
    GUID clsid{};
    if (!fromProgID(L"HNetCfg.FwPolicy2", &clsid))
        return nullptr;
    INetFwPolicy2* pol = nullptr;
    if (FAILED(CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER,
                                __uuidof(INetFwPolicy2), reinterpret_cast<void**>(&pol))))
        return nullptr;
    return pol;
}

// 遍历规则表，每条回调一次（名称 / 归一化后的程序路径 / 方向 / 动作 / 说明是不是 ltool 写的）
template <class Fn>
void forEachRule(INetFwPolicy2* pol, int* total, Fn fn)
{
    INetFwRules* rules = nullptr;
    if (FAILED(pol->get_Rules(&rules)) || !rules)
        return;
    IUnknown* unk = nullptr;
    if (SUCCEEDED(rules->get__NewEnum(&unk)) && unk) {
        IEnumVARIANT* en = nullptr;
        if (SUCCEEDED(unk->QueryInterface(IID_IEnumVARIANT, reinterpret_cast<void**>(&en))) && en) {
            VARIANT v;
            ULONG got = 0;
            while (en->Next(1, &v, &got) == S_OK && got == 1) {
                if (total)
                    ++*total;
                if (v.vt == VT_DISPATCH && v.pdispVal) {
                    INetFwRule* r = nullptr;
                    if (SUCCEEDED(v.pdispVal->QueryInterface(__uuidof(INetFwRule),
                                                             reinterpret_cast<void**>(&r))) && r) {
                        BSTR name = nullptr, app = nullptr, desc = nullptr;
                        NET_FW_RULE_DIRECTION dir = NET_FW_RULE_DIR_OUT;
                        NET_FW_ACTION action = NET_FW_ACTION_BLOCK;
                        r->get_Name(&name);
                        r->get_ApplicationName(&app);
                        r->get_Direction(&dir);
                        r->get_Action(&action);
                        r->get_Description(&desc);
                        fn(bstrToQt(name), normPath(bstrToQt(app)),
                           dir == NET_FW_RULE_DIR_IN, action == NET_FW_ACTION_ALLOW,
                           bstrToQt(desc).startsWith(QLatin1String("ltool")));
                        if (name) SysFreeString(name);
                        if (app) SysFreeString(app);
                        if (desc) SysFreeString(desc);
                        r->Release();
                    }
                }
                VariantClear(&v);
            }
            en->Release();
        }
        unk->Release();
    }
    rules->Release();
}

// 遍历规则，把匹配 exePath 的规则名收进来；同时报有没有"入站+允许"的
void scanRules(INetFwPolicy2* pol, const QString& exeLower, Firewall::Match* out)
{
    forEachRule(pol, &out->total, [&](const QString& name, const QString& exe,
                                      bool in, bool allow, bool ours) {
        if (exeLower.isEmpty() || exe != exeLower)
            return;
        out->any = true;
        if (in && allow)
            out->allowIn = true;
        if (name.isEmpty())
            return;
        out->names << name;
        if (ours)
            out->ours << name;        // 说明里带 ltool 的就是我们加的
    });
}

// 提权跑一次我们自己（UAC 会弹一次），子进程以管理员身份用 COM 写规则。
// 不用 netsh：实测 netsh 报"成功"但规则根本没落地（中文规则名 + 引号解析的坑），
// 而提权后的 COM 直加是一次就成的。
QString runElevatedSelf(const QString& params)
{
    wchar_t selfPath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, selfPath, MAX_PATH) == 0)
        return QStringLiteral("拿不到 ltool 自己的路径，没法提权重试");
    const std::wstring w = params.toStdWString();
    SHELLEXECUTEINFOW si{};
    si.cbSize = sizeof(si);
    si.fMask = SEE_MASK_NOCLOSEPROCESS;
    si.lpVerb = L"runas";
    si.lpFile = selfPath;
    si.lpParameters = w.c_str();
    si.nShow = SW_HIDE;
    if (!ShellExecuteExW(&si)) {
        const DWORD e = GetLastError();
        return e == ERROR_CANCELLED ? QStringLiteral("UAC 被取消了，规则没有写入")
                                    : QStringLiteral("提权启动失败（错误 %1）").arg(e);
    }
    if (si.hProcess) {
        WaitForSingleObject(si.hProcess, 120000);       // 等用户回答 UAC，别掐断
        CloseHandle(si.hProcess);
    }
    return {};
}

} // namespace

namespace Firewall {

QString ruleNameFor(const QString& displayName)
{
    QString n = displayName;
    n.remove(QLatin1Char('"'));
    // 安装包常把"版本 x.y.z"拼进 DisplayName，规则名里没必要带着
    static const QRegularExpression tail(
        QStringLiteral("[\\s]*(版本|version)[\\s]*[0-9][0-9a-zA-Z._\\-]*[\\s]*$"),
        QRegularExpression::CaseInsensitiveOption);
    n.remove(tail);
    n = n.trimmed();
    if (n.isEmpty())
        n = QStringLiteral("程序");
    if (n.size() > 100)
        n = n.left(100);
    return n;
}

QString exeKey(const QString& path)
{
    return normPath(path);
}

Match rulesFor(const QString& exePath)
{
    Match m;
    const QString lower = normPath(exePath);
    if (lower.isEmpty())
        return m;
    ComGuard com;
    INetFwPolicy2* pol = openPolicy();
    if (!pol)
        return m;
    scanRules(pol, lower, &m);
    pol->Release();
    return m;
}

QHash<QString, Match> rulesByExe()
{
    QHash<QString, Match> out;
    ComGuard com;
    INetFwPolicy2* pol = openPolicy();
    if (!pol)
        return out;
    int total = 0;
    forEachRule(pol, &total, [&](const QString& name, const QString& exe,
                                 bool in, bool allow, bool ours) {
        if (exe.isEmpty() || name.isEmpty())
            return;                 // 端口/程序组规则没有 exe，跳过
        Match& m = out[exe];
        m.total = total;
        m.any = true;
        if (in && allow)
            m.allowIn = true;
        m.names << name;
        if (ours)
            m.ours << name;
    });
    pol->Release();
    return out;
}

bool addRuleNow(const QString& exePath, const QString& ruleName)
{
    const QString exe = QDir::toNativeSeparators(Platform::expandEnv(exePath));
    ComGuard com;
    INetFwPolicy2* pol = openPolicy();
    if (!pol)
        return false;
    HRESULT addHr = E_FAIL;
    GUID clsid{};
    if (fromProgID(L"HNetCfg.FWRule", &clsid)) {
        INetFwRule* rule = nullptr;
        if (SUCCEEDED(CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER,
                                       __uuidof(INetFwRule), reinterpret_cast<void**>(&rule))) && rule) {
            rule->put_Name(Bstr(ruleName));
            rule->put_Description(Bstr(QStringLiteral(
                "ltool 添加 · 允许入站 · %1 · %2")
                .arg(QDate::currentDate().toString(Qt::ISODate), exe)));
            rule->put_ApplicationName(Bstr(exe));
            rule->put_Direction(NET_FW_RULE_DIR_IN);
            rule->put_Action(NET_FW_ACTION_ALLOW);
            rule->put_Enabled(VARIANT_TRUE);
            rule->put_Profiles(NET_FW_PROFILE2_ALL);
            INetFwRules* rules = nullptr;
            if (SUCCEEDED(pol->get_Rules(&rules)) && rules) {
                addHr = rules->Add(rule);
                rules->Release();
            }
            rule->Release();
        }
    }
    pol->Release();
    Utils::log(QString("firewall: COM add hr=0x%1 (rule %2)")
                   .arg(quint32(addHr), 8, 16, QLatin1Char('0')).arg(ruleName));
    return SUCCEEDED(addHr);
}

bool removeRuleNow(const QString& ruleName)
{
    ComGuard com;
    INetFwPolicy2* pol = openPolicy();
    if (!pol)
        return false;
    HRESULT hr = E_FAIL;
    INetFwRules* rules = nullptr;
    if (SUCCEEDED(pol->get_Rules(&rules)) && rules) {
        hr = rules->Remove(Bstr(ruleName));
        rules->Release();
    }
    pol->Release();
    Utils::log(QString("firewall: COM remove hr=0x%1 (rule %2)")
                   .arg(quint32(hr), 8, 16, QLatin1Char('0')).arg(ruleName));
    return SUCCEEDED(hr);
}

QString allowExe(const QString& exePath, const QString& displayName)
{
    const QString exe = QDir::toNativeSeparators(Platform::expandEnv(exePath));
    if (exe.isEmpty() || !QFileInfo::exists(exe))
        return QStringLiteral("找不到这个程序的文件，没法为它开规则：%1").arg(exePath);
    const QString name = ruleNameFor(displayName);

    // 已经有入站允许规则了就别再叠一条（名字用软件自己的名字，撞名概率比带前缀时高）
    const Match before = rulesFor(exe);
    if (before.allowIn)
        return QStringLiteral("这个程序本来就有入站允许规则了（%1），没有再加一条。")
            .arg(before.names.join(QStringLiteral("、")));

    if (!addRuleNow(exe, name)) {
        // 十有八九是权限不够（非管理员必吃 0x80070005）：把自己用 UAC 拉起来再写一次。
        // 不用 netsh —— 实测 netsh 会返回"成功"但规则根本没落地。
        Utils::log(QStringLiteral("firewall: 直加被拒，提权重跑一次自己"));
        const QString err = runElevatedSelf(
            QStringLiteral("--fwallow \"%1\" \"%2\"").arg(exe, name));
        Utils::log(QString("firewall: 提权结果 %1").arg(err.isEmpty() ? QStringLiteral("ok") : err));
        if (!err.isEmpty())
            return err;
    }

    for (int i = 0; i < 12; ++i) {
        const Match m = rulesFor(exe);
        if (m.allowIn)
            return QStringLiteral("已加入允许列表：%1%2").arg(name,
                       m.names.size() > 1 ? QStringLiteral("（这个程序一共有 %1 条规则）").arg(m.names.size())
                                                  : QString());
        QThread::msleep(400);
    }
    return QStringLiteral("命令执行完了，但回读防火墙规则时没看到这条 —— 可能是 UAC 被取消，或者规则被别的安全软件管着。");
}
QString removeRuleNamed(const QString& name)
{
    ComGuard com;
    INetFwPolicy2* pol = openPolicy();
    if (!pol)
        return QStringLiteral("连不上 Windows 防火墙服务");
    HRESULT hr = E_FAIL;
    INetFwRules* rules = nullptr;
    if (SUCCEEDED(pol->get_Rules(&rules)) && rules) {
        hr = rules->Remove(Bstr(name));
        rules->Release();
    }
    pol->Release();
    if (SUCCEEDED(hr))
        return QStringLiteral("已删除规则：%1").arg(name);
    const QString err = runElevatedSelf(QStringLiteral("--fwremove \"%1\"").arg(name));
    return err.isEmpty() ? QStringLiteral("已提交删除（走了一次 UAC）：%1").arg(name) : err;
}

} // namespace Firewall
