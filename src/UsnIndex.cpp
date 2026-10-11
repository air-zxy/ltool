#include "UsnIndex.h"

#include <QDir>

#include <windows.h>
#include <winioctl.h>

#include <cstdio>

namespace {

// 枚举/读日志的单次缓冲区：一次 ioctl 能带回成千上万条记录，别拿小buffer反复进内核
constexpr DWORD kBufBytes = 4u << 20;

bool openVolume(const QString& root, HANDLE* out)
{
    // 盘符 → \\.\C:
    QString drive = QDir::fromNativeSeparators(root);
    if (drive.length() >= 2 && drive[1] == QLatin1Char(':'))
        drive = drive.left(2);
    else
        return false;
    const std::wstring path = L"\\\\.\\" + drive.toStdWString() + L"\\";
    // 只要 FILE_READ_DATA 就够（读 MFT/日志），但打这个必须有管理员
    HANDLE h = CreateFileW(path.c_str(), FILE_READ_DATA | FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    *out = h;
    return true;
}

} // namespace

namespace UsnIndex {

bool VolumeHandle::open(const QString& root)
{
    if (vol != INVALID_HANDLE_VALUE)
        return true;
    HANDLE h = INVALID_HANDLE_VALUE;
    if (!openVolume(root, &h))
        return false;
    vol = h;
    return true;
}

void VolumeHandle::close()
{
    if (vol != INVALID_HANDLE_VALUE) {
        CloseHandle(vol);
        vol = INVALID_HANDLE_VALUE;
    }
}

VolumeHandle::~VolumeHandle()
{
    close();
}

JournalInfo queryJournal(HANDLE vol)
{
    JournalInfo info;
    USN_JOURNAL_DATA_V0 j{};
    DWORD ret = 0;
    if (DeviceIoControl(vol, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &j, sizeof(j), &ret, nullptr)
        && ret >= sizeof(j)) {
        info.ok = true;
        info.journalId = j.UsnJournalID;
        info.nextUsn = j.NextUsn;
    }
    return info;
}

// 一条 USN_RECORD_V2 里的名字（UTF-16，FileNameLength 按字节算，所以一定是 2 的倍数）
QString recordName(const BYTE* rec, const USN_RECORD_V2& r)
{
    return QString::fromUtf16(reinterpret_cast<const char16_t*>(rec + r.FileNameOffset),
                              int(r.FileNameLength / 2));
}

bool enumVolumeMft(HANDLE vol, EnumResult& out)
{
    const JournalInfo ji = queryJournal(vol);
    out.journal = ji;

    MFT_ENUM_DATA_V0 mft{};
    mft.StartFileReferenceNumber = 0;
    mft.LowUsn = 0;
    // Windows 26200 上必须把 HighUsn 设成日志的 NextUsn：留 0 的话枚举直接返回空
    mft.HighUsn = ji.nextUsn;

    QByteArray buf;
    buf.resize(kBufBytes);   // 先声明再 resize：`QByteArray buf(int(n))` 会被读成函数声明
    for (;;) {
        DWORD ret = 0;
        if (!DeviceIoControl(vol, FSCTL_ENUM_USN_DATA, &mft, sizeof(mft),
                             buf.data(), DWORD(buf.size()), &ret, nullptr))
            return !out.frn.isEmpty();        // 已经跑完时最后是 ERROR_HANDLE_EOF
        if (ret < sizeof(quint64) + sizeof(USN_RECORD_V2))
            break;

        // 返回值开头是下一次枚举的起点
        memcpy(&mft.StartFileReferenceNumber, buf.constData(), sizeof(quint64));
        const BYTE* p = reinterpret_cast<const BYTE*>(buf.constData()) + sizeof(quint64);
        const BYTE* end = reinterpret_cast<const BYTE*>(buf.constData()) + ret;
        while (p + sizeof(USN_RECORD_V2) <= end) {
            // SDK 的 USN_RECORD_V2 里"版本"就是 MajorVersion，长度字段叫 RecordLength
            const USN_RECORD_V2& r = *reinterpret_cast<const USN_RECORD_V2*>(p);
            if (r.MajorVersion != 2 || r.RecordLength < sizeof(USN_RECORD_V2))
                break;
            out.frn.append(r.FileReferenceNumber & 0x0000FFFFFFFFFFFFull);
            out.parent.append(r.ParentFileReferenceNumber & 0x0000FFFFFFFFFFFFull);
            out.attrs.append(r.FileAttributes);
            out.nameOff.append(qint32(out.names.size()));
            out.nameLen.append(qint32(r.FileNameLength));
            out.names.append(reinterpret_cast<const char*>(p + r.FileNameOffset), int(r.FileNameLength));
            p += r.RecordLength;
        }
    }
    return !out.frn.isEmpty();
}

bool readChanges(HANDLE vol, quint64 journalId, quint64 startUsn, quint64 endUsn,
                 QVector<RawChange>& out)
{
    READ_USN_JOURNAL_DATA_V0 rd{};
    rd.StartUsn = startUsn;
    rd.ReasonMask = 0xFFFFFFFF;
    rd.ReturnOnlyOnClose = 0;
    rd.Timeout = 0;
    rd.BytesToWaitFor = 0;          // 0 = 读到这里已有的，不等新记录
    rd.UsnJournalID = journalId;    // 对不上（日志被重建过）ioctl 直接失败

    QByteArray buf;
    buf.resize(kBufBytes);   // 先声明再 resize：`QByteArray buf(int(n))` 会被读成函数声明
    while (rd.StartUsn < endUsn) {
        DWORD ret = 0;
        if (!DeviceIoControl(vol, FSCTL_READ_USN_JOURNAL, &rd, sizeof(rd),
                             buf.data(), DWORD(buf.size()), &ret, nullptr))
            break;
        if (ret < sizeof(quint64))
            break;
        quint64 next = 0;
        memcpy(&next, buf.constData(), sizeof(quint64));
        const BYTE* p = reinterpret_cast<const BYTE*>(buf.constData()) + sizeof(quint64);
        const BYTE* end = reinterpret_cast<const BYTE*>(buf.constData()) + ret;
        int records = 0;
        while (p + sizeof(USN_RECORD_V2) <= end) {
            const USN_RECORD_V2& r = *reinterpret_cast<const USN_RECORD_V2*>(p);
            if (r.MajorVersion != 2 || r.RecordLength < sizeof(USN_RECORD_V2))
                break;
            ++records;
            if (r.Usn >= endUsn)
                return true;                  // 越过我们要的那段了，下一轮再续
            RawChange c;
            c.frn = r.FileReferenceNumber & 0x0000FFFFFFFFFFFFull;
            c.parentFrn = r.ParentFileReferenceNumber & 0x0000FFFFFFFFFFFFull;
            c.reason = r.Reason;
            c.attrs = r.FileAttributes;
            c.name = recordName(p, r);
            // $ 开头的流名/根目录自己不进变更表
            if (!c.name.startsWith(QLatin1Char('$')))
                out.append(c);
            p += r.RecordLength;
        }
        if (records == 0 || next <= rd.StartUsn)
            break;                            // 没前进就收工，别在这打转
        rd.StartUsn = next;
    }
    return true;
}

void mftEnumMatrix(HANDLE vol)
{
    // 把"日志开没开"和"几种 HighUsn 取法各能捞回多少条"摊开打印：
    // 这台机器上就是这么定位到"HighUsn 必须给 NextUsn"的（README 已知限制里那条）
    const JournalInfo ji = queryJournal(vol);
    std::printf("journal ok=%d id=%llu NextUsn=%llu\n", ji.ok ? 1 : 0,
                unsigned long long(ji.journalId), unsigned long long(ji.nextUsn));
    if (!ji.ok) {
        std::printf("  没有 USN 日志（这台机器/这个卷没开），枚举 MFT 无从谈起\n");
        return;
    }
    struct Case { const char* what; quint64 low; quint64 high; };
    const Case cases[] = {
        {"HighUsn=0", 0, 0},
        {"HighUsn=NextUsn", 0, ji.nextUsn},
        {"HighUsn=NextUsn-1", 0, ji.nextUsn - 1},
        {"LowUsn=NextUsn/2", ji.nextUsn / 2, ji.nextUsn},
    };
    for (const Case& c : cases) {
        MFT_ENUM_DATA_V0 mft{};
        mft.StartFileReferenceNumber = 0;
        mft.LowUsn = c.low;
        mft.HighUsn = c.high;
        QByteArray buf;
    buf.resize(kBufBytes);   // 先声明再 resize：`QByteArray buf(int(n))` 会被读成函数声明
        quint64 count = 0, frn = 0;
        for (int loop = 0; loop < 20000; ++loop) {
            DWORD ret = 0;
            mft.StartFileReferenceNumber = frn;
            if (!DeviceIoControl(vol, FSCTL_ENUM_USN_DATA, &mft, sizeof(mft),
                                 buf.data(), DWORD(buf.size()), &ret, nullptr))
                break;
            if (ret <= sizeof(quint64))
                break;
            memcpy(&frn, buf.constData(), sizeof(quint64));
            const BYTE* p = reinterpret_cast<const BYTE*>(buf.constData()) + sizeof(quint64);
            const BYTE* end = reinterpret_cast<const BYTE*>(buf.constData()) + ret;
            while (p + sizeof(USN_RECORD_V2) <= end) {
                const USN_RECORD_V2& r = *reinterpret_cast<const USN_RECORD_V2*>(p);
                if (r.RecordLength < sizeof(USN_RECORD_V2))
                    break;
                ++count;
                p += r.RecordLength;
            }
        }
        std::printf("  %-18s → %llu 条记录 (err=%lu)\n", c.what,
                    unsigned long long(count), GetLastError());
        std::fflush(stdout);
    }
}

} // namespace UsnIndex
