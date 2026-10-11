const fs = require('fs');
const p = 'D:/project/cpp/ltool/src/FileOps.cpp';
let s = fs.readFileSync(p, 'utf8');

function cut(startMark, endMark, replacement) {
  const a = s.indexOf(startMark);
  if (a < 0) throw new Error('start missing: ' + startMark.slice(0, 50));
  if (s.indexOf(startMark, a + startMark.length) >= 0)
    throw new Error('start AMBIGUOUS: ' + startMark.slice(0, 50));
  const b = s.indexOf(endMark, a);
  if (b < 0) throw new Error('end missing: ' + endMark.slice(0, 50));
  s = s.slice(0, a) + replacement + s.slice(b);
  console.log('cut ' + (b - a) + ' chars');
}

// 1) 干掉：变长对象类型表的猜测解析、设备路径换算、句柄表结构
cut('int fileObjectTypeIndex()', 'bool fillRandom(', '');
cut('// 句柄表里的对象名是', '\nQString processNameOf', '');
cut('using NtQuerySystemInformationFn', '\nstruct XHandleEntry', '');
cut('struct XHandleEntry', '\nQString processNameOf', '');

// 2) 换掉 whoHas 本体
cut('QVector<Holder> whoHas(const QString& path, QString* err)',
    'bool killProcess(quint64 pid, QString* err)',
    [
'// 谁开着这个文件：用 Windows 的 Restart Manager（rstrtmgr）—— 它就是为这个查询设计的，',
'// 毫秒级返回。以前自己遍历系统句柄表的做法要给每个文件句柄 DuplicateHandle + NtQueryObject，',
'// 实测在这台机器上跑几分钟都出不来，还会卡在管道句柄上。',
'QVector<Holder> whoHas(const QString& path, QString* err)',
'{',
'    QVector<Holder> out;',
'    const QString native = QDir::toNativeSeparators(path);',
'    std::wstring w = native.toStdWString();',
'    DWORD session = 0;',
'    wchar_t key[CCH_RM_SESSION_KEY + 1] = {};',
'    UINT rc = RmStartSession(&session, 0, key);',
'    if (rc != ERROR_SUCCESS) {',
'        if (err)',
'            *err = QStringLiteral("启动 Restart Manager 会话失败（%1）").arg(rc);',
'        return out;',
'    }',
'    LPCWSTR targets[1] = { w.c_str() };',
'    rc = RmRegisterResources(session, 1, targets, 0, nullptr, 0, nullptr);',
'    if (rc != ERROR_SUCCESS) {',
'        if (err)',
'            *err = QStringLiteral("登记要查的文件失败（%1）").arg(rc);',
'        RmEndSession(session);',
'        return out;',
'    }',
'    DWORD needed = 0, processes = 0;',
'    UINT total = 0;',
'    rc = RmGetList(session, &needed, &processes, nullptr, &total);',
'    if (rc == ERROR_SUCCESS && total == 0) {',
'        RmEndSession(session);',
'        return out;                        // 确实没人开着',
'    }',
'    if (rc != ERROR_MORE_DATA && rc != ERROR_SUCCESS) {',
'        if (err)',
'            *err = QStringLiteral("查询占用进程失败（%1）").arg(rc);',
'        RmEndSession(session);',
'        return out;',
'    }',
'    QVector<RM_PROCESS_INFO> infos(size_t(needed));',
'    processes = needed;',
'    total = 0;',
'    rc = RmGetList(session, &needed, &processes, infos.data(), &total);',
'    RmEndSession(session);',
'    if (rc != ERROR_SUCCESS) {',
'        if (err)',
'            *err = QStringLiteral("取占用进程列表失败（%1）").arg(rc);',
'        return out;',
'    }',
'    QHash<quint64, Holder> byPid;',
'    for (UINT i = 0; i < total; ++i) {',
'        const RM_PROCESS_INFO& pi = infos[int(i)];',
'        const quint64 pid = quint64(pi.process.dwProcessId);',
'        Holder& hh = byPid[pid];',
'        if (hh.pid == 0) {',
'            hh.pid = pid;',
'            hh.processName = QString::fromWCharArray(pi.strAppName).trimmed();',
'            if (hh.processName.isEmpty())',
'                hh.processName = processNameOf(DWORD(pid));',
'            hh.exePath = processExeOf(DWORD(pid));',
'        }',
'        ++hh.handles;',
'        const QString reason = QString::fromWCharArray(pi.strServiceShortName).trimmed();',
'        if (!reason.isEmpty() && hh.objects.size() < 4)',
'            hh.objects << reason;',
'    }',
'    for (auto& h : byPid)',
'        out.append(h);',
'    std::sort(out.begin(), out.end(), [](const Holder& a, const Holder& b) {',
'        return a.handles > b.handles;',
'    });',
'    return out;',
'}',
'',
''
].join('\n'));

// 3) 头文件：RestartManager
s = s.replace('#include <tlhelp32.h>', '#include <tlhelp32.h>\n#include <RestartManager.h>');
fs.writeFileSync(p, s, 'utf8');
console.log('whoHas rewritten');
