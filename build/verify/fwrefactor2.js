const fs = require('fs');
const Q = String.fromCharCode(34);           // double quote
const BS = String.fromCharCode(92);           // backslash
const NL0 = '\n';

const path = 'D:/project/cpp/ltool/src/Firewall.cpp';
let s = fs.readFileSync(path, 'utf8');
const NL = s.includes('\r\n') ? '\r\n' : '\n';
const j = lines => lines.join(NL);
const AP = String.fromCharCode(39);

const start = s.indexOf('QString allowExe(const QString& exePath, const QString& displayName)');
const end = s.indexOf('QString removeRuleNamed(const QString& name)');
if (start < 0 || end < 0 || end < start) throw new Error('boundaries');

const bodyRaw = j([
'bool addRuleNow(const QString& exePath, const QString& ruleName)',
'{',
'    const QString exe = QDir::toNativeSeparators(Utils::expandEnv(exePath));',
'    ComGuard com;',
'    INetFwPolicy2* pol = openPolicy();',
'    if (!pol)',
'        return false;',
'    HRESULT addHr = E_FAIL;',
'    GUID clsid{};',
'    if (fromProgID(L"HNetCfg.FWRule", &clsid)) {',
'        INetFwRule* rule = nullptr;',
'        if (SUCCEEDED(CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER,',
'                                       __uuidof(INetFwRule), reinterpret_cast<void**>(&rule))) && rule) {',
'            rule->put_Name(Bstr(ruleName));',
'            rule->put_Description(Bstr(QStringLiteral("ltool 软件列表里加的入站允许规则")));',
'            rule->put_ApplicationName(Bstr(exe));',
'            rule->put_Direction(NET_FW_RULE_DIR_IN);',
'            rule->put_Action(NET_FW_ACTION_ALLOW);',
'            rule->put_Enabled(VARIANT_TRUE);',
'            rule->put_Profiles(NET_FW_PROFILE2_ALL);',
'            INetFwRules* rules = nullptr;',
'            if (SUCCEEDED(pol->get_Rules(&rules)) && rules) {',
'                addHr = rules->Add(rule);',
'                rules->Release();',
'            }',
'            rule->Release();',
'        }',
'    }',
'    pol->Release();',
'    Utils::log(QString("firewall: COM add hr=0x%1 (rule %2)")',
'                   .arg(quint32(addHr), 8, 16, QLatin1Char(TOK)).arg(ruleName));',
'    return SUCCEEDED(addHr);',
'}',
'',
'bool removeRuleNow(const QString& ruleName)',
'{',
'    ComGuard com;',
'    INetFwPolicy2* pol = openPolicy();',
'    if (!pol)',
'        return false;',
'    HRESULT hr = E_FAIL;',
'    INetFwRules* rules = nullptr;',
'    if (SUCCEEDED(pol->get_Rules(&rules)) && rules) {',
'        hr = rules->Remove(Bstr(ruleName));',
'        rules->Release();',
'    }',
'    pol->Release();',
'    Utils::log(QString("firewall: COM remove hr=0x%1 (rule %2)")',
'                   .arg(quint32(hr), 8, 16, QLatin1Char(TOK)).arg(ruleName));',
'    return SUCCEEDED(hr);',
'}',
'',
'QString allowExe(const QString& exePath, const QString& displayName)',
'{',
'    const QString exe = QDir::toNativeSeparators(Utils::expandEnv(exePath));',
'    if (exe.isEmpty() || !QFileInfo::exists(exe))',
'        return QStringLiteral("找不到这个程序的文件，没法为它开规则：%1").arg(exePath);',
'    const QString name = ruleNameFor(displayName);',
'',
'    if (!addRuleNow(exe, name)) {',
'        // 十有八九是权限不够（非管理员必吃 0x80070005）：把自己用 UAC 拉起来再写一次。',
'        // 不用 netsh —— 实测 netsh 会返回"成功"但规则根本没落地。',
'        Utils::log(QStringLiteral("firewall: 直加被拒，提权重跑一次自己"));',
'        const QString err = runElevatedSelf(',
'            QStringLiteral("--fwallow ' + BS + Q + '%1' + BS + Q + ' ' + BS + Q + '%2' + BS + Q + '").arg(exe, name));',
'        Utils::log(QString("firewall: 提权结果 %1").arg(err.isEmpty() ? QStringLiteral("ok") : err));',
'        if (!err.isEmpty())',
'            return err;',
'    }',
'',
'    for (int i = 0; i < 12; ++i) {',
'        const Match m = rulesFor(exe);',
'        if (m.allowIn)',
'            return QStringLiteral("已加入允许列表：%1%2").arg(name,',
'                       m.names.size() > 1 ? QStringLiteral("（这个程序一共有 %1 条规则）").arg(m.names.size())',
'                                                  : QString());',
'        QThread::msleep(400);',
'    }',
'    return QStringLiteral("命令执行完了，但回读防火墙规则时没看到这条 —— 可能是 UAC 被取消，或者规则被别的安全软件管着。");',
'}',
'',
]);

const body = bodyRaw.split('TOK').join(AP + '0' + AP);
s = s.slice(0, start) + body + s.slice(end);

// removeRuleNamed 的兜底也换成提权调自己：按位置切，别拿转义引号当锚点
{
    const k = s.indexOf('advfirewall firewall delete rule name=');
    if (k < 0) throw new Error('remove fallback missing');
    const lineStart = s.lastIndexOf('const QString err', k);
    const tail = '.arg(name));';
    const lineEnd = s.indexOf(tail, k) + tail.length;
    s = s.slice(0, lineStart)
        + 'const QString err = runElevatedSelf(QStringLiteral("--fwremove ' + BS + Q + '%1' + BS + Q + '").arg(name));'
        + s.slice(lineEnd);
}

fs.writeFileSync(path, s);
console.log('rewritten; runNetshElevated left:', s.includes('runNetshElevated'));
