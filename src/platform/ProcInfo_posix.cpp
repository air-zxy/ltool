#include "platform/ProcInfo.h"

#include <QFile>

#include <QDir>
#include <QFileInfo>
#include <QList>

#include <csignal>
#include <unistd.h>

namespace Platform {

namespace {

// "谁是它的直接子进程"：Linux 在 /proc/<pid>/task/<tid>/children 里给一串空格分隔的 pid；
// 那个文件读不到（权限、或不是主线程的 tid）就退化成扫 /proc 比 PPid。
QList<qint64> childPids(qint64 pid)
{
    QList<qint64> out;
    QFile ch(QStringLiteral("/proc/%1/task/%1/children").arg(pid));
    if (ch.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> cols = ch.readAll().simplified().split(' ');
        for (const QByteArray& c : cols) {
            const qint64 v = c.toLongLong();
            if (v > 0)
                out << v;
        }
        if (!out.isEmpty())
            return out;
    }
    for (const QFileInfo& fi : QDir(QStringLiteral("/proc")).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool isNum = false;
        const qint64 candidate = fi.fileName().toLongLong(&isNum);
        if (!isNum || candidate == pid)
            continue;
        QFile st(fi.filePath() + QStringLiteral("/status"));
        if (!st.open(QIODevice::ReadOnly))
            continue;
        while (!st.atEnd()) {
            const QByteArray line = st.readLine();
            if (line.startsWith("PPid:")) {
                if (line.mid(5).trimmed().toLongLong() == pid)
                    out << candidate;
                break;
            }
        }
    }
    return out;
}

} // namespace

bool killProcessTree(qint64 pid)
{
    if (pid <= 0)
        return false;
    bool killed = false;
    for (const qint64 c : childPids(pid))
        killed = killProcessTree(c) || killed;
    if (::kill(static_cast<pid_t>(pid), SIGKILL) == 0)
        killed = true;
    return killed;
}

int workingSetMB()
{
    // /proc/self/statm 第二列是常驻页数（RSS），乘页大小就是任务管理器里那个"内存"
    QFile f(QStringLiteral("/proc/self/statm"));
    if (!f.open(QIODevice::ReadOnly))
        return -1;
    const QList<QByteArray> cols = f.readAll().simplified().split(' ');
    if (cols.size() < 2)
        return -1;
    const qint64 pages = cols[1].toLongLong();
    const long page = sysconf(_SC_PAGESIZE);
    if (pages <= 0 || page <= 0)
        return -1;
    return int(pages * page / (1024 * 1024));
}

bool trimWorkingSet()
{
    // 做不到，也不假装：Windows 那个 SetProcessWorkingSetSize(-1,-1) 在 POSIX 上没有对等物，
    // malloc_trim 只还堆顶、动不了内核记着的常驻页。返回 false 让调用方说实话。
    return false;
}

QStringList runningImageNames()
{
    // 这一颗在 POSIX 上**还没写**（不是"做不到"：读 /proc/<pid>/exe 是可行的），
    // 所以返回空。界面上那颗按钮拿到空表要照实说"这台机器列不出进程"，
    // 不许摆一张空列表骗人以为"没东西可忽略"。
    return {};
}

} // namespace Platform
