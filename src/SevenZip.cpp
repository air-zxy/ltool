#include "SevenZip.h"

#include "Utils.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include <bit7z/bit7zlibrary.hpp>
#include <bit7z/bitexception.hpp>
#include <bit7z/bitfilecompressor.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using namespace bit7z;

// 这个 vcpkg 构建没开 BIT7Z_USE_NATIVE_STRING，所以 bit7z 的 tstring 是 char 的串，
// 它按 UTF-8 收进去、自己转成 UTF-16 交给 7z.dll（中文条目名就靠这一步）。
std::string u8(const QString& s)
{
    return s.toUtf8().toStdString();
}

// 打包漏了它 / 被杀软隔走的时候给一句人话。为什么要先查文件而不是等它自己抛：
// 实测藏掉 7zip.dll 直接跑压缩，进程是 0xC0000409 走的，一行输出都没留下（try 接不住
// 那一条 —— 崩在 bit7z 装载库的路上），界面上就是"点了没反应"。
QString libraryPath()
{
    return QDir::toNativeSeparators(
        QCoreApplication::applicationDirPath() + QStringLiteral("/7zip.dll"));
}

// 库句柄：Bit7zLibrary 只是一个 HMODULE + CreateObject 入口，进程里建一次大家共用。
const Bit7zLibrary& library()
{
    static const std::unique_ptr<Bit7zLibrary> lib = std::make_unique<Bit7zLibrary>(u8(libraryPath()));
    return *lib;
}

// 7-Zip 的档位只认它自己枚举里那几个值（BitCompressionLevel：0/1/3/5/7/9），
// 外面传的是习惯的 0-9，中间值就近落到有定义的那一档。
BitCompressionLevel levelOf(int level)
{
    if (level <= 0)
        return BitCompressionLevel::None;
    if (level <= 2)
        return BitCompressionLevel::Fastest;
    if (level <= 4)
        return BitCompressionLevel::Fast;
    if (level <= 6)
        return BitCompressionLevel::Normal;
    if (level <= 8)
        return BitCompressionLevel::Max;
    return BitCompressionLevel::Ultra;
}

// 多线程编码器会从不同线程报"现在轮到哪个文件"，界面每帧要读它 —— 加锁取一份副本，
// 别的都不锁（进度本身是原子量）。
class CurrentName {
public:
    void set(const QString& name)
    {
        const std::lock_guard<std::mutex> lock(mMutex);
        mValue = name;
    }
    QString get() const
    {
        const std::lock_guard<std::mutex> lock(mMutex);
        return mValue;
    }

private:
    mutable std::mutex mMutex;
    QString mValue;
};

} // namespace

namespace SevenZip {

Archive::Result compress(const QStringList& inputs, const QString& destFile, int level,
                         const Archive::Report& report, const Archive::Cancel& cancel)
{
    Archive::Result res;
    const QString nativeDest = QDir::toNativeSeparators(destFile);
    const QString partPath = nativeDest + QStringLiteral(".part");

    if (!QFileInfo::exists(libraryPath())) {
        res.error = QStringLiteral("压缩这一步要用的 7zip.dll 不在软件目录里（%1）。\n"
                                   "解压、zip、其它功能都不受影响，重装一次就能补回来。")
                        .arg(libraryPath());
        Utils::log(QStringLiteral("sevenzip: %1").arg(res.error));
        return res;
    }

    // 先自己数一遍：文件数/目录数/总字节是界面进度条的分母，也是完成后那句话的内容。
    // 只在这里数一次（单一出处），bit7z 的回调不再另算一份。
    std::vector<std::string> members;
    qint64 totalBytes = 0;
    for (const QString& in : inputs) {
        const QFileInfo fi(in);
        if (!fi.exists())
            continue;
        members.push_back(u8(QDir::toNativeSeparators(in)));
        if (!fi.isDir()) {
            totalBytes += fi.size();
            ++res.files;
            continue;
        }
        // 数的口径跟 libarchive 那条路对齐：目录本身算一项，里面每个文件算一项
        ++res.dirs;
        QDirIterator dit(in, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                         QDirIterator::Subdirectories);
        while (dit.hasNext()) {
            dit.next();
            ++res.dirs;
        }
        QDirIterator fit(in, QDir::Files | QDir::NoSymLinks | QDir::Hidden | QDir::System,
                         QDirIterator::Subdirectories);
        while (fit.hasNext()) {
            totalBytes += QFileInfo(fit.next()).size();
            ++res.files;
        }
    }
    if (members.empty()) {
        res.error = QStringLiteral("要打包的东西已经不在了");
        return res;
    }

    QFile::remove(partPath);   // 上一次的半成品不能当这一次的底子

    std::atomic<qint64> done{ 0 };
    std::atomic<bool> stopped{ false };
    CurrentName current;

    // 整段都在 try 里：库句柄建不出来（7zip.dll 不在/坏了）也是从这里抛的。
    // 漏在 try 外面时它一路走到 abort()，实测退出码 0xC0000409、一行输出都没有 ——
    // 界面只会看到"什么都没发生"。
    try {
        BitFileCompressor comp{ library(), BitFormat::SevenZip };
        comp.setCompressionLevel(levelOf(level));
        comp.setThreadsCount(uint32_t(std::thread::hardware_concurrency()));

        comp.setFileCallback([&current](const tstring& path) {
            current.set(QFileInfo(QString::fromUtf8(path.c_str(), int(path.size()))).fileName());
        });
        comp.setProgressCallback([&](const uint64_t processed) {
            if (stopped.load())
                return false;
            const qint64 n = qint64(processed);
            done.store(n);
            if (cancel && cancel()) {
                stopped.store(true);
                return false;
            }
            if (report && !report(Archive::Progress{ n, totalBytes, current.get() })) {
                stopped.store(true);   // 界面只有在按了取消时才返回 false
                return false;
            }
            return true;
        });
        comp.compress(members, u8(partPath));
    } catch (const std::exception& e) {
        QFile::remove(partPath);   // 半成品别留在旁边装成成品
        const QString msg = QString::fromUtf8(e.what());
        if (stopped.load()) {
            res.cancelled = true;
            res.error = QStringLiteral("已取消，半成品压缩包已删掉");
            Utils::log(QStringLiteral("sevenzip: 取消 %1").arg(nativeDest));
        } else {
            res.error = QStringLiteral("7-Zip 编码器没压出来：%1").arg(msg);
            Utils::log(QStringLiteral("sevenzip: 压缩失败 %1 -> %2").arg(nativeDest, msg));
        }
        return res;
    }

    res.bytes = done.load();
    QString renameErr;
    if (!Utils::renameStaged(partPath, nativeDest, &renameErr)) {
        res.error = renameErr;
        res.target = partPath;
        return res;
    }
    res.ok = true;
    res.target = nativeDest;
    return res;
}

} // namespace SevenZip
