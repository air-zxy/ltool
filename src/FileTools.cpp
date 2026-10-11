#include "FileTools.h"

#include "FileOps.h"

#include <QCoreApplication>

namespace FileTools {

QVector<Lock> findLocks(const QString& path)
{
    QVector<Lock> out;
    QString err;
    const auto holders = FileOps::whoHas(path, &err);
    for (const FileOps::Holder& h : holders) {
        Lock l;
        l.name = h.processName.isEmpty()
                     ? QStringLiteral("进程 %1").arg(h.pid) : h.processName;
        l.pid = h.pid;
        out.append(l);
    }
    return out;
}

// 把"还留着哪些没结束掉"拼成一句界面能直接显示的话
static QString killAll(const QVector<quint64>& pids)
{
    QStringList failed;
    for (quint64 pid : pids) {
        QString err;
        if (!FileOps::killProcess(pid, &err))
            failed << QStringLiteral("pid %1：%2").arg(pid).arg(err);
    }
    return failed.join("\n");
}

QString unlock(const QString& path)
{
    QString whoErr;
    const auto holders = FileOps::whoHas(path, &whoErr);
    if (holders.isEmpty())
        return whoErr;            // 枚举本身失败时把原因说清楚，别报"已全部结束"
    QVector<quint64> pids;
    for (const FileOps::Holder& h : holders)
        if (!pids.contains(h.pid))
            pids << h.pid;
    return killAll(pids);
}

QString unlockByPid(quint64 pid)
{
    return killAll({pid});
}

QString shred(const QString& path)
{
    QString err;
    // thorough=true：调用点那句话已经告诉用户"三轮覆写并永久删除"了，这里必须真做三遍
    if (FileOps::shred(path, true, &err) < 0)
        return err.isEmpty() ? QStringLiteral("粉碎失败") : err;
    return QString();
}

} // namespace FileTools
