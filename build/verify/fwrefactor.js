const fs = require('fs');
function patch(path, reps) {
  let s = fs.readFileSync(path, 'utf8');
  const NL = s.includes('\r\n') ? '\r\n' : '\n';
  for (let [a, b] of reps) {
    a = a.split('\n').join(NL); b = b.split('\n').join(NL);
    if (!s.includes(a)) throw new Error('anchor missing in ' + path + ': ' + JSON.stringify(a.slice(0, 70)));
    s = s.replace(a, b);
  }
  fs.writeFileSync(path, s);
  console.log('patched', path);
}

patch('D:/project/cpp/ltool/src/Firewall.h', [
  ['QString allowExe(const QString& exePath, const QString& displayName);',
   'QString allowExe(const QString& exePath, const QString& displayName);\n' +
   '\n' +
   '// 只做一次 COM 写入，不自己提权：给"被 UAC 拉起来的子进程"和上面的 allowExe 复用\n' +
   'bool addRuleNow(const QString& exePath, const QString& ruleName);\n' +
   'bool removeRuleNow(const QString& ruleName);'],
]);

patch('D:/project/cpp/ltool/src/Firewall.cpp', [
  // allowExe 里的 COM 添加段抽成 addRuleNow
  ['QString allowExe(const QString& exePath, const QString& displayName)\n' +
   '{\n' +
   '    const QString exe = QDir::toNativeSeparators(Utils::expandEnv(exePath));\n' +
   '    if (exe.isEmpty() || !QFileInfo::exists(exe))\n' +
   '        return QStringLiteral("找不到这个程序的文件，没法为它开规则：%1").arg(exePath);\n' +
   '\n' +
   '    ComGuard com;\n' +
   '    INetFwPolicy2* pol = openPolicy();\n' +
   '    if (!pol)\n' +
   '        return QStringLiteral("连不上 Windows 防火墙服务（mpsdrv / Base Filtering Engine 可能在被停用）");\n' +
   '\n' +
   '    const QString name = ruleNameFor(displayName);\n' +
   '    HRESULT addHr = E_FAIL;',
   'bool addRuleNow(const QString& exePath, const QString& ruleName)\n' +
   '{\n' +
   '    const QString exe = QDir::toNativeSeparators(Utils::expandEnv(exePath));\n' +
   '    ComGuard com;\n' +
   '    INetFwPolicy2* pol = openPolicy();\n' +
   '    if (!pol)\n' +
   '        return false;\n' +
   '    HRESULT addHr = E_FAIL;'],
  ['    pol->Release();\n' +
   '\n' +
   '    Utils::log(QString("firewall: COM add hr=0x%1 (rule %2)")\n' +
   '                       .arg(quint32(addHr), 8, 16, QLatin1Char(\'0\')).arg(name));\n' +
   '    if (FAILED(addHr)) {\n' +
   '        Utils::log(QStringLiteral("firewall: 走提权 netsh -> %1").arg(args));\n' +
   '        const QString err = runNetshElevated(args);\n' +
   '        Utils::log(QString("firewall: netsh 结果 %1").arg(err.isEmpty() ? QStringLiteral("ok") : err));\n' +
   '        if (!err.isEmpty())\n' +
   '            return err;\n' +
   '    }',
   '    pol->Release();\n' +
   '    Utils::log(QString("firewall: COM add hr=0x%1 (rule %2)")\n' +
   '                       .arg(quint32(addHr), 8, 16, QLatin1Char(\'0\')).arg(ruleName));\n' +
   '    return SUCCEEDED(addHr);\n' +
   '}\n' +
   '\n' +
   'QString allowExe(const QString& exePath, const QString& displayName)\n' +
   '{\n' +
   '    const QString exe = QDir::toNativeSeparators(Utils::expandEnv(exePath));\n' +
   '    if (exe.isEmpty() || !QFileInfo::exists(exe))\n' +
   '        return QStringLiteral("找不到这个程序的文件，没法为它开规则：%1").arg(exePath);\n' +
   '    const QString name = ruleNameFor(displayName);\n' +
   '\n' +
   '    if (!addRuleNow(exe, name)) {\n' +
   '        // 十有八九是权限不够：把自己用 UAC 拉起来再写一次\n' +
   '        Utils::log(QStringLiteral("firewall: 直加被拒，提权重跑一次自己"));\n' +
   '        const QString err = runElevatedSelf(QStringLiteral("--fwallow \\"%1\\" \\"%2\\"").arg(exe, name));\n' +
   '        Utils::log(QString("firewall: 提权结果 %1").arg(err.isEmpty() ? QStringLiteral("ok") : err));\n' +
   '        if (!err.isEmpty())\n' +
   '            return err;\n' +
   '    }'],
]);
