#pragma once

#include <QString>
#include <QVector>

#include <windows.h>

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

// 读 [startUsn, endUsn) 这段变更。journalId 对不上（日志被重建过）时返回 false。
bool readChanges(HANDLE vol, quint64 journalId, quint64 startUsn, quint64 endUsn,
                 QVector<RawChange>& out);

// 诊断用（--mfttest）：把 FSCTL_ENUM_USN_DATA 的几种参数组合各跑一遍并把条数打出来。
// 本机 Windows 26200 上踩过一个坑：HighUsn 必须设成日志的 NextUsn，否则枚举直接空。
void mftEnumMatrix(HANDLE vol);

} // namespace UsnIndex
