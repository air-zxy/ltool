#pragma once

#include "FileOps.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

#include <algorithm>

// 平台层第七道缝：粉碎（shred）那一路。
//
// 分叉点只有两个动作，所以只抽这两个：
//   shredOne       —— 把这一个文件覆写掉再删（thorough 时连名字也换掉）；失败原因归平台说
//   removeEmptyDir —— 删掉一个已经空了的目录
// 「先走一遍文件、目录从最深往上删、最后删自己」这套顺序两边一模一样，就写在这一份里，
// 不抄第二遍 —— 抄来的第二遍迟早和另一份分叉（这条规矩在本项目已经量过一次代价了）。
//
// 占用查询（whoHas）和结束进程（killProcess）没有可共用的骨架：Windows 用 Restart Manager
// 一次问出来，POSIX 只能扫 /proc/<pid>/fd 逐个比对 inode，两边各写一份在 FileOps_win.cpp /
// FileOps_posix.cpp 里。
namespace FileOps {

// 覆写并删除这一个文件。erased 加上抹掉的字节数（按文件大小算一遍，不是按覆写次数）。
// 失败时若 err 非空，里面是平台那句原因（Windows 带错误码，POSIX 带 strerror）。
bool shredOne(const QString& file, bool thorough, qint64* erased, QString* err);
bool removeEmptyDir(const QString& dir, QString* err);

// 公开那颗 FileOps::shred 就是它的包装（两份平台文件各写一行转过来）。
inline qint64 shredWalk(const QString& path, bool thorough, QString* err)
{
    const QFileInfo fi(path);
    if (!fi.exists()) {
        if (err)
            *err = QStringLiteral("文件不存在：%1").arg(path);
        return -1;
    }
    qint64 erased = 0;
    if (fi.isDir()) {
        QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks,
                        QDirIterator::Subdirectories);
        QStringList files;
        while (it.hasNext())
            files << it.next();
        for (const QString& f : files) {
            qint64 sub = 0;
            // 目录里每一颗的失败原因一律说成这句：那一串路径后面还要接着删，
            // 平台细节（错误码）在这里帮不上用户，以前就是这么摆的，不改行为。
            if (!shredOne(f, thorough, &sub, nullptr)) {
                if (err)
                    *err = QStringLiteral("这个文件没能覆写删除（可能被占用或没有写权限）：%1").arg(f);
                return erased > 0 ? erased : -1;
            }
            erased += sub;
        }
        // 从最深的目录往上删
        QDirIterator dit(path, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                         QDirIterator::Subdirectories);
        QStringList dirs;
        while (dit.hasNext())
            dirs << dit.next();
        std::sort(dirs.begin(), dirs.end(), [](const QString& a, const QString& b) {
            return a.size() > b.size();
        });
        dirs << QDir::toNativeSeparators(path);
        for (const QString& d : dirs) {
            if (!removeEmptyDir(d, nullptr) && err)
                *err = QStringLiteral("目录删不掉（里面可能还有别人刚写进去的东西）：%1").arg(d);
        }
        return erased;
    }
    if (!shredOne(QDir::toNativeSeparators(path), thorough, &erased, err))
        return -1;
    return erased;
}

} // namespace FileOps
