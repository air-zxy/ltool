// 平台层第七道缝，POSIX 这一份：粉碎 / 占用查询 / 结束进程。
//
// 三件都是"机制不同、目标相同"，不是降级：
//  · 覆写用 open(O_WRONLY) + write + fsync + ftruncate，删除用 unlink；改名用 rename。
//    Windows 那句 SetFileAttributesW(NORMAL) 这里没有对等物也不需要 —— POSIX 上删文件看的
//    是**所在目录**的写权限，不是文件属性；真删不掉就是 EACCES/EPERM，原话打在 err 里。
//  · 占用查询没有 Restart Manager 那样的官方入口，只能扫 /proc/<pid>/fd 把每个句柄指向的
//    东西 stat 一遍，按 (st_dev, st_ino) 对 —— 按路径字符串对会被 rename 和符号链接骗过。
//    两个已知边界要说清：① 不是 root 只看得到同一用户的进程（别人的目录读不了，返回空表不是
//    "没人占用"）；② 只用 mmap 打开、或者以相对路径持有的，这条路认不全。
//  · 结束进程没有 TerminateProcess 那种"立刻断"的入口：先 SIGTERM 给它 2 秒自己收尾，
//    还活着再 SIGKILL。这里只杀点名的那一颗（和 Windows 那份一致），不连子孙 ——
//    要连子孙的是 Platform::killProcessTree（"取消安装"那一路用），两件事别混。
#include "platform/FileOpsShred.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <random>

namespace FileOps {

namespace {

QString errnoText(int e)
{
    return QString::fromUtf8(std::strerror(e));
}

// 覆写一整段：zeros=true 全零，否则随机。1MB 一块，和 Windows 那份同一档。
bool fillFd(int fd, qint64 bytes, bool zeros, std::mt19937_64& rng)
{
    QByteArray chunk(1 << 20, 0);
    if (!zeros) {
        // 每写一遍都换一批随机数（Windows 那份是每遍一个新 rng，行为一致）
        for (int i = 0; i + 8 <= chunk.size(); i += 8)
            *reinterpret_cast<quint64*>(chunk.data() + i) = rng();
    }
    qint64 left = bytes;
    while (left > 0) {
        const qint64 take = qMin<qint64>(left, chunk.size());
        qint64 done = 0;
        while (done < take) {
            const ssize_t w = ::write(fd, chunk.constData() + done, size_t(take - done));
            if (w <= 0) {
                if (errno == EINTR)
                    continue;
                return false;
            }
            done += w;
        }
        left -= take;
    }
    return ::fsync(fd) == 0;
}

bool isPidName(const QString& s)
{
    if (s.isEmpty())
        return false;
    for (QChar c : s)
        if (!c.isDigit())
            return false;
    return true;
}

// 这个 pid 还在不在（僵尸算没了：它手里已经没有文件句柄，占用查询也不会再报它）。
// kill(pid,0) 对别人的进程返回 EPERM —— 那不能当成"不在了"。
bool alive(int pid)
{
    if (::kill(pid, 0) != 0)
        return errno == EPERM;
    QFile st(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!st.open(QIODevice::ReadOnly))
        return true;
    const QString body = QString::fromUtf8(st.readAll());
    // 那一行是 "pid (comm) state ..."，comm 里能带空格和括号，所以从最后一个 ')' 之后取
    const int r = body.lastIndexOf(QLatin1Char(')'));
    if (r < 0)
        return true;
    return body.mid(r + 1).trimmed().left(1) != QLatin1Char('Z');
}

QString procComm(quint64 pid)
{
    QFile f(QStringLiteral("/proc/%1/comm").arg(pid));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

// Qt 6.9 才有 QFile::symlinkTarget，WSL 那套是 6.4.2 —— 这个 TU 只在 POSIX 上编，
// 就直接读 readlink，少一个 Qt 版本依赖。
QString readLink(const QString& path)
{
    const QByteArray p = path.toLocal8Bit();
    QByteArray buf(4096, '\0');
    for (;;) {
        const ssize_t n = ::readlink(p.constData(), buf.data(), size_t(buf.size()));
        if (n < 0)
            return {};
        if (n < buf.size())
            return QString::fromLocal8Bit(buf.constData(), int(n));
        buf.resize(buf.size() * 2);
    }
}

} // namespace

bool shredOne(const QString& file, bool thorough, qint64* erased, QString* err)
{
    const QByteArray p = file.toLocal8Bit();
    const int fd = ::open(p.constData(), O_WRONLY);      // 没有 O_CREAT：文件不在就该在上面就报错
    if (fd < 0) {
        if (err)
            *err = errno == EACCES || errno == EPERM
                       ? QStringLiteral("没有写权限，覆写不了。先取消只读、或者用管理员运行 ltool")
                       : QStringLiteral("粉碎失败（%1）").arg(errnoText(errno));
        return false;
    }
    struct stat st{};
    qint64 size = 0;
    if (::fstat(fd, &st) == 0 && S_ISREG(st.st_mode))
        size = qint64(st.st_size);
    bool ok = true;
    std::mt19937_64 rng(std::random_device{}());
    const int passes = thorough ? 3 : 1;
    for (int pass = 0; pass < passes && ok; ++pass) {
        if (::lseek(fd, 0, SEEK_SET) < 0) { ok = false; break; }
        ok = fillFd(fd, size, pass == 1, rng);           // 中间一遍全 0，和 Windows 那份同一顺序
        if (ok && ::fsync(fd) != 0) ok = false;
    }
    // 截断：让"文件大小"这个元信息也不指向原来的数据块
    if (::ftruncate(fd, 0) != 0)
        ok = false;
    ::close(fd);
    if (ok)
        *erased += size;

    if (thorough) {                            // 连名字（可能带原始文件名）也抹掉
        const QFileInfo fi(file);
        const QString junk = fi.dir().absoluteFilePath(
            QStringLiteral("~%1.tmp").arg(quint64(std::random_device{}()) * 2654435761ULL, 12, 16, QLatin1Char('0')));
        if (::rename(file.toLocal8Bit().constData(), junk.toLocal8Bit().constData()) != 0) {
            if (err)
                *err = QStringLiteral("改不成乱名字（%1）：%2").arg(errnoText(errno), file);
            return false;
        }
        if (::unlink(junk.toLocal8Bit().constData()) != 0 || !ok) {
            if (err)
                *err = QStringLiteral("粉碎失败（%1）").arg(errnoText(errno));
            return false;
        }
        return true;
    }
    if (::unlink(p.constData()) != 0 || !ok) {
        if (err)
            *err = QStringLiteral("粉碎失败（%1）").arg(errnoText(errno));
        return false;
    }
    return true;
}

bool removeEmptyDir(const QString& dir, QString*)
{
    return ::rmdir(dir.toLocal8Bit().constData()) == 0;
}

qint64 shred(const QString& path, bool thorough, QString* err)
{
    return shredWalk(path, thorough, err);
}

QVector<Holder> whoHas(const QString& path, QString* err)
{
    QVector<Holder> out;
    const QString target = QDir::cleanPath(path);
    const QByteArray tp = target.toLocal8Bit();
    struct stat want{};
    if (::stat(tp.constData(), &want) != 0) {
        if (err)
            *err = QStringLiteral("读不到这个文件（%1）").arg(errnoText(errno));
        return out;
    }
    QDir proc(QStringLiteral("/proc"));
    if (!proc.exists()) {
        if (err)
            *err = QStringLiteral("这里没有 /proc，占用查询只能返回空表");
        return out;
    }
    const QString deletedSuffix = QStringLiteral(" (deleted)");
    QHash<quint64, Holder> byPid;
    const QStringList pids = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& pidName : pids) {
        if (!isPidName(pidName))
            continue;
        // 读不到别人的 fd 目录 = 权限不够，不是"没人开着"，这里跳过（界面上那句限制已经写明）
        QDir fdDir(QStringLiteral("/proc/%1/fd").arg(pidName));
        const QStringList fds = fdDir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot
                                                | QDir::System | QDir::Hidden);
        for (const QString& fdName : fds) {
            const QString link = readLink(fdDir.absoluteFilePath(fdName));
            if (link.isEmpty())
                continue;
            QString name = link;
            bool gone = false;
            if (name.endsWith(deletedSuffix)) {
                name.chop(deletedSuffix.size());
                gone = true;
            }
            bool same = false;
            if (gone) {
                same = QDir::cleanPath(name) == target;      // 已经删掉的没法 stat，只能比路径
            } else {
                struct stat got{};
                same = ::stat(name.toLocal8Bit().constData(), &got) == 0
                       && got.st_dev == want.st_dev && got.st_ino == want.st_ino;
            }
            if (!same)
                continue;
            const quint64 pid = pidName.toULongLong();
            Holder& hh = byPid[pid];
            if (hh.pid == 0) {
                hh.pid = pid;
                hh.exePath = readLink(QStringLiteral("/proc/%1/exe").arg(pidName));
                hh.processName = procComm(pid);
                if (hh.processName.isEmpty() && !hh.exePath.isEmpty())
                    hh.processName = hh.exePath.section(QLatin1Char('/'), -1);
                if (hh.processName.isEmpty())
                    hh.processName = QStringLiteral("pid %1").arg(pid);
            }
            ++hh.handles;
            if (gone && hh.objects.size() < 4)
                hh.objects << QStringLiteral("文件已删除，但它的句柄还开着");
        }
    }
    for (auto& h : byPid)
        out.append(h);
    std::sort(out.begin(), out.end(), [](const Holder& a, const Holder& b) {
        return a.handles > b.handles;
    });
    return out;
}

bool killProcess(quint64 pid, QString* err)
{
    if (pid == 0 || pid > quint64(0x7FFFFFF0)) {
        if (err)
            *err = QStringLiteral("这个进程号用不了：%1").arg(pid);
        return false;
    }
    const int p = int(pid);
    if (::kill(p, SIGTERM) != 0) {
        if (err)
            *err = errno == EPERM
                       ? QStringLiteral("结束不了：权限不够（那个进程可能是别的用户或 root 起的）")
                       : QStringLiteral("没有那个进程");
        return false;
    }
    for (int i = 0; i < 40; ++i) {          // 2 秒（每 50ms 看一次）
        if (!alive(p))
            return true;
        ::usleep(50 * 1000);
    }
    if (::kill(p, SIGKILL) != 0) {
        if (err)
            *err = QStringLiteral("强制结束也失败（%1）").arg(errnoText(errno));
        return false;
    }
    for (int i = 0; i < 20; ++i) {          // 再等 1 秒
        if (!alive(p))
            return true;
        ::usleep(50 * 1000);
    }
    if (err)
        *err = QStringLiteral("SIGKILL 之后它还在（多半成了僵尸，父进程没回收）");
    return false;
}

} // namespace FileOps
