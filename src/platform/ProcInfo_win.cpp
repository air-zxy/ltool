#include "platform/ProcInfo.h"

#include "Utils.h"

#include <QSet>

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>

namespace Platform {

bool killProcessTree(qint64 pid)
{
    if (pid <= 0)
        return false;
    bool killed = false;
    // 先深度优先杀子孙：winget 会再拉起 AppInstaller/msiexec/setup.exe，
    // 只杀它一个的话安装照样跑完，"取消"就成了假动作
    if (const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W ent{};
        ent.dwSize = sizeof(ent);
        if (Process32FirstW(snap, &ent)) {
            do {
                if ((qint64)ent.th32ProcessID != pid
                    && (qint64)ent.th32ParentProcessID == pid)
                    killed = killProcessTree((qint64)ent.th32ProcessID) || killed;
            } while (Process32NextW(snap, &ent));
        }
        CloseHandle(snap);
    }
    if (const HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid)) {
        killed = TerminateProcess(h, 1602 /* ERROR_CANCELLED */) || killed;
        CloseHandle(h);
    }
    return killed;
}

int workingSetMB()
{
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (!K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
        return -1;
    return int(pmc.WorkingSetSize / (1024 * 1024));
}

bool trimWorkingSet()
{
    const BOOL ok = SetProcessWorkingSetSize(GetCurrentProcess(), SIZE_T(-1), SIZE_T(-1));
    Utils::log(QString("trim working set: %1 (剩 %2MB)").arg(ok ? "ok" : QString("failed err %1").arg(GetLastError()))
                   .arg(workingSetMB()));
    return ok != FALSE;
}

QStringList runningImageNames()
{
    QStringList out;
    QSet<QString> seen;
    if (const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W ent{};
        ent.dwSize = sizeof(ent);
        if (Process32FirstW(snap, &ent)) {
            do {
                const QString name = QString::fromWCharArray(ent.szExeFile);
                if (name.isEmpty() || seen.contains(name.toLower()))
                    continue;      // 同一个 exe 跑好几分（浏览器、IM 都这样），表里只要一行
                seen.insert(name.toLower());
                out << name;
            } while (Process32NextW(snap, &ent));
        }
        CloseHandle(snap);
    }
    out.sort(Qt::CaseInsensitive);
    return out;
}

} // namespace Platform
