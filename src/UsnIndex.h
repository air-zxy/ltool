#pragma once

#include <QString>
#include <QVector>

#include <windows.h>

#include <functional>

// USN 日志 / MFT 枚举这一层的裸 Win32 封装。
// 打开卷和读日志都**要管理员权限**：打不开时 open() 返回 false，调用方自己回退到
// 目录遍历（并把 GetLastError() 打进日志），所以这里一律不弹框不打日志，只回报成败。
namespace UsnIndex {

// 卷的日志信息。ok=false 表示这台机器/这个卷没开 USN 日志。
struct JournalInfo {
    bool ok = false;
    quint64 journalId = 0;
    quint64 nextUsn = 0;
};

// RAII 的卷句柄（\\.\C:）。禁止拷贝，出作用域自动关。
struct VolumeHandle {
    HANDLE vol = INVALID_HANDLE_VALUE;
    bool open(const QString& root);
    void close();
    ~VolumeHandle();
    VolumeHandle() = default;
    VolumeHandle(const VolumeHandle&) = delete;
    VolumeHandle& operator=(const VolumeHandle&) = delete;
};

// 一次 MFT 全量枚举的结果，按"平行数组"存（几百万条时比 QVector<struct> 省得多）：
//   frn[i]    这条记录自己的文件引用号
//   parent[i] 它父目录的 frn
//   attrs[i]  FILE_ATTRIBUTE_*
//   names 里第 i 条的名字在 nameOff[i] 起、占 nameLen[i] 字节（UTF-16，所以一定是 2 的倍数）
struct EnumResult {
    QVector<quint64> frn;
    QVector<quint64> parent;
    QVector<quint32> attrs;
    QVector<qint32> nameOff;
    QVector<qint32> nameLen;
    QByteArray names;
    JournalInfo journal;
};

// 日志里的一条变更
struct RawChange {
    quint64 frn = 0;
    quint64 parentFrn = 0;
    QString name;               // 已经转成 UTF-16→QString 的文件名（不含路径）
    quint32 reason = 0;         // USN_REASON_* 的组合
    quint32 attrs = 0;
};

bool enumVolumeMft(HANDLE vol, EnumResult& out);
JournalInfo queryJournal(HANDLE vol);

// 把 $MFT 顺序读一遍，取回 want 里每个记录号的**真实大小**（sizes 与 want 同序）。
// 为什么要单独这一遍：FSCTL_ENUM_USN_DATA 只给结构（谁在谁下面、叫什么），大小不在里面 ——
// 它在每条 FILE 记录的 $STANDARD_INFORMATION 里（Real Size 那一格）。
// want 里同一个号出现多次（硬链接）会各自填上同一个值，跟目录遍历"一个名字算一份"的口径一致。
// progress 每读完一块调一次，返回 false 表示取消（取消时 sizes 不可信，函数返回 false）。
using SizeProgress = std::function<bool(qint64 recordsSeen, qint64 recordsTotal)>;
bool mftSizes(const QString& root, const QVector<quint64>& want, QVector<qint64>& sizes,
              const SizeProgress& progress = {});

// 这台机器现在能不能走 MFT 这条路：卷打不开（没提权）、或卷上没有 USN 日志，都不行。
// why 填人话给界面用（界面必须说清"这次为什么慢"，别只说"扫描完成"）。
bool mftUsable(const QString& root, QString* why = nullptr);

// 读 [startUsn, endUsn) 这段变更。journalId 对不上（日志被重建过）时返回 false。
bool readChanges(HANDLE vol, quint64 journalId, quint64 startUsn, quint64 endUsn,
                 QVector<RawChange>& out);


} // namespace UsnIndex
