const fs = require('fs');
const p = 'D:/project/cpp/ltool/src/FileOps.cpp';
let s = fs.readFileSync(p, 'utf8');

const oldA = [
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
'    }'
].join('\n');
const newA = [
'    // 先问要几个，再取（rgAffectedApps 传 nullptr 就是量大小）',
'    UINT needed = 0, filled = 0;',
'    DWORD reasons = 0;',
'    rc = RmGetList(session, &needed, &filled, nullptr, &reasons);',
'    if (rc != ERROR_SUCCESS && rc != ERROR_MORE_DATA && rc != ERROR_INVALID_PARAMETER) {',
'        if (err)',
'            *err = QStringLiteral("查询占用进程失败（%1）").arg(rc);',
'        RmEndSession(session);',
'        return out;',
'    }',
'    if (needed == 0) {',
'        RmEndSession(session);',
'        return out;                        // 确实没人开着',
'    }',
'    QVector<RM_PROCESS_INFO> infos(size_t(needed));',
'    filled = needed;',
'    reasons = 0;',
'    rc = RmGetList(session, &needed, &filled, infos.data(), &reasons);',
'    RmEndSession(session);',
'    if (rc != ERROR_SUCCESS) {',
'        if (err)',
'            *err = QStringLiteral("取占用进程列表失败（%1）").arg(rc);',
'        return out;',
'    }'
].join('\n');
if (s.indexOf(oldA) < 0) throw new Error('block A not found');
s = s.replace(oldA, newA);

const oldLoop = [
'    QHash<quint64, Holder> byPid;',
'    for (UINT i = 0; i < total; ++i) {'
].join('\n');
const newLoop = [
'    QHash<quint64, Holder> byPid;',
'    for (UINT i = 0; i < filled; ++i) {'
].join('\n');
if (s.indexOf(oldLoop) < 0) throw new Error('loop head not found');
s = s.replace(oldLoop, newLoop);

fs.writeFileSync(p, s, 'utf8');
console.log('RmGetList call fixed');
