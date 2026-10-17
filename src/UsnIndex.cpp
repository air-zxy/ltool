#include "UsnIndex.h"

#include <QDir>

#include <windows.h>
#include <winioctl.h>

#include <cstdio>
#include <array>
#include <vector>

namespace {

// 枚举/读日志的单次缓冲区：一次 ioctl 能带回成千上万条记录，别拿小buffer反复进内核
constexpr DWORD kBufBytes = 4u << 20;

// 读 $MFT 的单次块：4MB 是 256~4096 所有记录长度的整数倍，记录不会被劈在两块之间
constexpr DWORD kMftChunk = 4u << 20;

// 盘符 → "C:"；给的若不是**卷根**返回 false。
// 这一条卡得很严是有原因的：MFT 枚举出来的一定是整盘，拿"C:/Windows"当卷根的话，
// 拼出来的树会挂着 C:/Windows 的名字装的却是 C 盘全部内容。
bool volumeDrive(const QString& root, QString* drive)
{
    const QString d = QDir::fromNativeSeparators(root);
    if (d.length() < 2 || d.length() > 3 || d[1] != QLatin1Char(':') || !d[0].isLetter())
        return false;
    if (d.length() == 3 && d[2] != QLatin1Char('/'))
        return false;
    *drive = d.left(2);
    return true;
}

bool openVolume(const QString& root, HANDLE* out)
{
    // 盘符 → \\.\C:（**结尾不能再带反斜杠**：`\\.\C:\` 会被当成"卷根目录的路径"，
    // CreateFileW 直接给 ERROR_PATH_NOT_FOUND(3)。2026-10-17 实测四种写法量出来的：
    // `\\.\C:` → 5(ACCESS_DENIED，只差提权，形式是对的)；`\\.\C:\` → 3；`C:\$MFT` → 5）
    QString drive;
    if (!volumeDrive(root, &drive))
        return false;
    const std::wstring path = L"\\\\.\\" + drive.toStdWString();
    // 只要 FILE_READ_DATA 就够（读 MFT/日志），但打这个必须有管理员
    HANDLE h = CreateFileW(path.c_str(), FILE_READ_DATA | FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    *out = h;
    return true;
}

quint16 le16(const BYTE* p, int off)
{
    quint16 v = 0;
    memcpy(&v, p + off, 2);
    return v;
}

quint32 le32(const BYTE* p, int off)
{
    quint32 v = 0;
    memcpy(&v, p + off, 4);
    return v;
}

quint64 le64(const BYTE* p, int off)
{
    quint64 v = 0;
    memcpy(&v, p + off, 8);
    return v;
}

// $MFT 的 DACL 在现代 Windows 上只给 SYSTEM —— 提权的 Administrators 也开不了
// （2026-10-17 CI 实测：`\\.\C:\$MFT` 和 `C:\$MFT` 都给 5，`\\.\C:$MFT` 给 2）。
// 能绕过 DACL 的正规通道是**备份语义**：进程持有 SeBackupPrivilege（管理员默认有、但是关着的），
// 开句柄时带 FILE_FLAG_BACKUP_SEMANTICS。这个 RAII 只在开这一颗句柄的那几微秒里把它打开，
// 出作用域立刻关掉 —— 不让整个进程长时间带着一个能绕过 ACL 读任何文件的特权。
class BackupPrivilege
{
public:
    BackupPrivilege()
    {
        HANDLE tok = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok))
            return;
        TOKEN_PRIVILEGES tp{};
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (!LookupPrivilegeValueW(nullptr, SE_BACKUP_NAME, &tp.Privileges[0].Luid)) {
            CloseHandle(tok);
            return;
        }
        if (AdjustTokenPrivileges(tok, FALSE, &tp, sizeof(tp), nullptr, nullptr)
            && GetLastError() == ERROR_SUCCESS)
            m_tok = tok;
        else
            CloseHandle(tok);
    }
    ~BackupPrivilege()
    {
        if (!m_tok)
            return;
        TOKEN_PRIVILEGES off{};
        off.PrivilegeCount = 1;
        off.Privileges[0].Attributes = 0;      // 关掉
        if (LookupPrivilegeValueW(nullptr, SE_BACKUP_NAME, &off.Privileges[0].Luid))
            AdjustTokenPrivileges(m_tok, FALSE, &off, sizeof(off), nullptr, nullptr);
        CloseHandle(m_tok);
    }
    BackupPrivilege(const BackupPrivilege&) = delete;
    BackupPrivilege& operator=(const BackupPrivilege&) = delete;
    bool held() const { return m_tok != nullptr; }

private:
    HANDLE m_tok = nullptr;
};

// 这一段看着像不像一条 FILE 记录（用来验 $MFT 的记录长度，不靠猜）
bool looksLikeFileRecord(const BYTE* rec, int recBytes)
{
    if (memcmp(rec, "FILE", 4) != 0)
        return false;
    const int firstAttr = le16(rec, 20);
    const int usaOff = le16(rec, 4);
    const int usaCount = le16(rec, 6);
    return firstAttr >= 42 && firstAttr < recBytes && usaCount >= 2 && usaCount <= 64
           && usaOff + 2 * usaCount <= recBytes;
}

// $STANDARD_INFORMATION（一定在记录里第一个属性、且常驻）里的 Real Size。
// 记录头：0x04 usaOff / 0x06 usaCount / 0x14 firstAttr / 0x16 flags（bit0=在用）
// 属性通用头：0x00 type / 0x04 length / 0x08 非驻留标志 / 0x10 值起点 / 0x12 值长度
// SI 体：四个 8 字节时间戳 → 0x20 分配大小 / 0x28 真实大小
bool recordRealSize(BYTE* rec, int recBytes, qint64* out)
{
    if (!(le16(rec, 22) & 0x01))          // 空闲记录：里面全是上一个文件的垃圾
        return false;
    // 更新序列：每个扇区最后 2 字节被挪进表里了，读之前先放回去。
    // 我们只取第一个扇区里的 SI 时用不上，但记录可能是 2048/4096 字节、属性起点不固定。
    const int usaOff = le16(rec, 4);
    for (int k = 1; k < le16(rec, 6); ++k) {
        const int at = k * 512 - 2;
        if (at + 2 > recBytes || usaOff + 2 * k + 2 > recBytes)
            break;
        memcpy(rec + at, rec + usaOff + 2 * k, 2);
    }
    for (int off = le16(rec, 20); off + 8 <= recBytes;) {
        const quint32 type = le32(rec, off);
        const quint32 len = le32(rec, off + 4);
        if (type == 0xFFFFFFFFu || len < 8 || off + int(len) > recBytes)
            break;
        if (type == 0x80) {
            if (rec[off + 8])             // 非驻留的 SI：这台机器不该有，宁可报"取不到"
                return false;
            const int at = off + le16(rec, off + 0x10);
            if (le32(rec, off + 0x12) < 0x30 || at + 0x30 > recBytes)
                return false;
            *out = qint64(le64(rec, at + 0x28));
            return true;
        }
        off += int(len);
    }
    return false;
}

// 自己声明这三颗小结构，不引 <winternl.h>：那个头会连带拽进一批和 windows.h 打架的定义，
// 而我们只需要 NtCreateFile 的前三个参数形状（字段顺序/大小按 ntdll 的公开约定）。
struct MyUnicodeString { USHORT Length; USHORT MaximumLength; PWSTR Buffer; };
struct MyObjectAttributes {
    ULONG Length;
    HANDLE RootDirectory;
    MyUnicodeString* ObjectName;
    ULONG Attributes;
    PVOID SecurityDescriptor;
    PVOID SecurityQualityOfService;
};
struct MyIoStatusBlock { LONG Status; ULONG_PTR Information; };   // LONG = NTSTATUS 的本尊

// 按**记录号**开 $MFT：卷句柄 + FILE_OPEN_BY_FILE_ID。
// 为什么非走这条：名字那条路在现代 Windows 上连"提权 + 备份语义"都不给（2026-10-17 CI 实测
// 三种写法 × 两种语义全是 5），而 NTFS 支持用记录号从卷根下面取文件 —— 备份/索引类工具都走这颗。
// 记录号必须是**完整**的那颗（低 48 位记录号 + 高 16 位序列号），序列号从枚举里原样拿，不能自己补 0。
HANDLE openMftByRecordId(HANDLE vol, quint64 frn)
{
    constexpr ULONG kFileOpen = 1;
    constexpr ULONG kSyncNonAlert = 0x00000020;
    constexpr ULONG kOpenByFileId = 0x00002000;
    if (!vol || vol == INVALID_HANDLE_VALUE || !frn)
        return INVALID_HANDLE_VALUE;
    using NtCreateFileFn = LONG(WINAPI*)(HANDLE*, ACCESS_MASK, MyObjectAttributes*,
                                             MyIoStatusBlock*, PLARGE_INTEGER, ULONG, ULONG, ULONG,
                                             ULONG, PVOID, ULONG);
    static const auto ntCreate = reinterpret_cast<NtCreateFileFn>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateFile"));
    if (!ntCreate)
        return INVALID_HANDLE_VALUE;

    WCHAR id[4];                       // 8 字节 = 那颗记录号，UNICODE_STRING 只是把它当"名字"递进去
    memcpy(id, &frn, sizeof(id));
    MyUnicodeString name;
    name.Buffer = id;
    name.Length = name.MaximumLength = USHORT(sizeof(id));
    MyObjectAttributes oa{};
    oa.Length = sizeof(oa);
    oa.RootDirectory = vol;
    oa.ObjectName = &name;
    MyIoStatusBlock iosb{};
    HANDLE h = INVALID_HANDLE_VALUE;
    const LONG st = ntCreate(&h, FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE, &oa,
                                 &iosb, nullptr, FILE_ATTRIBUTE_NORMAL,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, kFileOpen,
                                 kSyncNonAlert | kOpenByFileId, nullptr, 0);
    if (st != 0 || h == INVALID_HANDLE_VALUE) {
        SetLastError(DWORD(st));       // 把 NTSTATUS 原样带出去（0xC0000022=拒绝访问这类才分得清）
        return INVALID_HANDLE_VALUE;
    }
    return h;
}

// $MFT 在这个卷上占哪几段（盘区段）。这是**唯一不碰 $MFT 权限**的读法：
// 名字打不开（DACL 只给 SYSTEM，提权 + 备份语义都试过了）、枚举里也没有它的记录号，
// 但卷句柄本身能按簇裸读 —— 引导扇区给出 MFT 的起始簇和记录长度，
// 记录 1（就是 $MFT 自己那条 FILE 记录）里的 $DATA 给出后面所有的段。
struct MftExtent {
    quint64 byteOff;
    quint64 bytes;
};

bool readAt(HANDLE h, quint64 off, void* dst, quint32 n)
{
    LARGE_INTEGER li{};
    li.QuadPart = qint64(off);
    DWORD got = 0;
    return SetFilePointerEx(h, li, nullptr, FILE_BEGIN) && ReadFile(h, dst, n, &got, nullptr)
           && got == n;
}

// NTFS 的 run list：每段 = [头字节][长度(簇数, 小端无符号)][起点(LCN 增量, 有符号)]，头字节低 4 位
// 是长度占的字节数、高 4 位是增量占的字节数，低 4 位为 0 表示结束。返回吃掉的字节数，解不动返回 -1。
int decodeRuns(const BYTE* p, int avail, QVector<MftExtent>& out, qint64 clusterBytes)
{
    int i = 0;
    qint64 lcn = 0;
    while (i < avail) {
        const int lenBits = p[i] & 0x0F, offBits = p[i] >> 4;
        if (!lenBits)
            return i + 1;
        if (i + 1 + lenBits + offBits > avail)
            return -1;
        quint64 cnt = 0;
        for (int k = 0; k < lenBits; ++k)
            cnt |= quint64(p[i + 1 + k]) << (8 * k);
        qint64 delta = 0;
        for (int k = 0; k < offBits; ++k)
            delta |= qint64(p[i + 1 + lenBits + k]) << (8 * k);
        if (offBits && ((delta >> (8 * offBits - 1)) & 1))
            delta -= (qint64(1) << (8 * offBits));      // 增量是有符号的，做符号扩展
        lcn += delta;
        out.append({ quint64(lcn) * quint64(clusterBytes), cnt * quint64(clusterBytes) });
        i += 1 + lenBits + offBits;
    }
    return -1;
}

bool mftExtents(HANDLE vol, qint64 clusterBytes, QVector<MftExtent>& runs, qint64* recordBytes,
                const QString& drive, QString* why)
{
    const auto no = [&](const QString& s) {
        if (why)
            *why = QStringLiteral("按簇读 $MFT 没走通（%1）").arg(s);
        return false;
    };
    BYTE boot[512];
    if (!readAt(vol, 0, boot, sizeof(boot)))
        return no(QStringLiteral("读不到引导扇区，错误码 %1").arg(GetLastError()));
    if (memcmp(boot + 3, "NTFS    ", 8) != 0)
        return no(QStringLiteral("%1 的引导扇区上不是 NTFS 签名").arg(drive));
    // $MFT 起始簇在引导扇区里那一格的偏移，各家文档给的数不一致（差一个字节就读出
    // "记录长度 1007616"这种鬼话 —— 2026-10-17 CI 实测）。所以：
    //   ① 起始簇按几个候选偏移挨个试，**认"那个位置读出来是不是一串排着队的 FILE 记录"**；
    //   ② 记录长度不再信引导扇区那格，直接从 FILE 签名的周期量出来；
    //   ③ 全都不成，把 0x28~0x48 那 32 字节原样打进失败原因 —— 看一眼就能手算出正确偏移，
    //      不必再拿一次 CI 去猜。
    static const int kLcnCands[] = { 0x2B, 0x2F, 0x30, 0x33, 0x38, 0x40 };
    qint64 mftLcn = -1, rec = 0;
    std::vector<BYTE> r1;
    QString tried;
    for (const int off : kLcnCands) {
        const qint64 lcn = qint64(le64(boot, off));
        if (lcn <= 0 || lcn > (1ll << 40)) {
            tried += QStringLiteral("%1:不像数 ").arg(off, 0, 16);
            continue;
        }
        std::vector<BYTE> blk;
        blk.resize(16384);
        if (!readAt(vol, quint64(lcn) * quint64(clusterBytes), blk.data(), UINT32(blk.size()))) {
            tried += QStringLiteral("%1:读不动 ").arg(off, 0, 16);
            continue;
        }
        int period = 0;
        for (const int r : { 256, 512, 1024, 2048, 4096 }) {
            if (3 * r + 4 > int(blk.size()))
                break;
            if (looksLikeFileRecord(blk.data(), r) && looksLikeFileRecord(blk.data() + r, r)
                && looksLikeFileRecord(blk.data() + 2 * r, r)
                && looksLikeFileRecord(blk.data() + 3 * r, r)) {
                period = r;
                break;
            }
        }
        if (!period) {
            tried += QStringLiteral("%1(簇 %2):没周期 ").arg(off, 0, 16).arg(lcn);
            continue;
        }
        mftLcn = lcn;
        rec = period;
        r1.resize(size_t(rec));
        memcpy(r1.data(), blk.data() + rec, size_t(rec));   // 第 1 号记录就在刚读这一块里
        break;
    }
    if (!rec) {
        QString hex;
        for (int i = 0x28; i < 0x50; ++i)
            hex += QStringLiteral("%1 ").arg(boot[i], 2, 16, QLatin1Char('0'));
        return no(QStringLiteral("引导扇区里 %1 都不是 MFT 起点 · 0x28~0x50 原样：%2")
                      .arg(tried.trimmed(), hex.trimmed()));
    }
    // 非驻留 $DATA 的盘区段紧跟在属性名之后。名字长度为 0 时按约定在 0x28，
    // 但不同版本这张表的读法有出入，所以 0x28/0x30/0x38/0x40 各试一遍，
    // **认"解出来的首簇等不等于引导扇区里那个 MFT 起点"** —— 对不上就是解错了，不猜。
    for (int off = le16(r1.data(), 20); off + 16 <= int(rec);) {
        const quint32 type = le32(r1.data(), off), len = le32(r1.data(), off + 4);
        if (type == 0xFFFFFFFFu || len < 16 || off + int(len) > int(rec))
            break;
        if (type == 0x80 && r1[off + 8]) {
            const int nameLen = r1[off + 9];
            const int nameOff = le16(r1.data() + off, 0x0A);
            const qint64 dataLen = qint64(le64(r1.data() + off, 0x18));
            for (const int cand : { nameOff + nameLen * 2, 0x28, 0x30, 0x38, 0x40 }) {
                const int aligned = (cand + 7) & ~7;
                if (aligned < 16 || off + aligned >= int(rec))
                    continue;
                QVector<MftExtent> got;
                const int used = decodeRuns(r1.data() + off + aligned, int(rec) - off - aligned, got,
                                            clusterBytes);
                if (used <= 0 || got.isEmpty())
                    continue;
                if (got.first().byteOff / quint64(clusterBytes) != quint64(mftLcn))
                    continue;
                qint64 sum = 0;
                for (const MftExtent& e : got)
                    sum += qint64(e.bytes);
                if (sum < dataLen)
                    continue;
                runs = got;
                *recordBytes = rec;
                if (why)
                    *why = QStringLiteral("盘区段解开了：%1 段、共 %2 字节（记录 %3 字节，起点在属性偏移 0x%4）")
                               .arg(got.size()).arg(sum).arg(rec).arg(aligned, 0, 16);
                return true;
            }
            return no(QStringLiteral("记录 1 里的 $DATA 找到了（属性在 0x%1、名字偏移 0x%2 名字长 %3、"
                                      "值长 %4），可盘区段怎么都解不对（首簇对不上 %5）· 属性头 128 字节：%6")
                          .arg(off, 0, 16).arg(nameOff, 0, 16).arg(nameLen).arg(dataLen).arg(mftLcn)
                          .arg([&] {
                              QString hex;
                              for (int i = off; i < int(rec) && i < off + 128; ++i)
                                  hex += QStringLiteral("%1").arg(r1[i], 2, 16, QLatin1Char('0'));
                              return hex;
                          }()));
        }
        off += int(len);
    }
    return no(QStringLiteral("第 1 号记录里没有非驻留的 $DATA"));
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
            // 顺手记下 $MFT 自己的完整记录号（含序列号）：后面按记录号开它要用
            if (!out.mftFrn && r.FileNameLength == 8
                && memcmp(p + r.FileNameOffset, u"$MFT", 8) == 0)
                out.mftFrn = r.FileReferenceNumber;
            p += r.RecordLength;
        }
    }
    return !out.frn.isEmpty();
}

bool mftUsable(const QString& root, QString* why)
{
    VolumeHandle vol;
    if (!vol.open(root)) {
        const DWORD e = GetLastError();
        if (why)
            *why = e == ERROR_ACCESS_DENIED
                ? QStringLiteral("没有管理员权限，打不开卷（错误码 %1）").arg(e)
                : QStringLiteral("打不开卷 %1（错误码 %2）").arg(root).arg(e);
        return false;
    }
    if (!queryJournal(vol.vol).ok) {
        if (why)
            *why = QStringLiteral("这个卷没开 USN 日志，NTFS 元数据无从枚举");
        return false;
    }
    return true;
}

bool mftSizes(const QString& root, const QVector<quint64>& want, QVector<qint64>& sizes,
                        const SizeProgress& progress, QString* why, quint64 mftFrn)
{
    // 每条退出路径都要留下"卡在哪一格"。CI 上那次只看得见"8 毫秒返回、全 0"，
    // 分不出是开不了 $MFT、记录长度没认出来、还是解析没对上 —— 那三种该修的地方完全不同。
    const auto no = [&](const QString& s) {
        if (why)
            *why = s;
        return false;
    };
    sizes = QVector<qint64>(want.size(), 0);
    if (want.isEmpty())
        return true;
    QString drive;
    if (!volumeDrive(root, &drive))
        return no(QStringLiteral("给的还不是卷根：%1").arg(root));

    quint64 maxFrn = 0;
    for (const quint64 f : want)
        maxFrn = qMax(maxFrn, f);
    if (maxFrn > (1ull << 32))
        return no(QStringLiteral("记录号大得不像话（%1）—— 枚举读出来的东西不可信").arg(maxFrn));

    // 记录号 → want 下标：head/next 串成链表，一次遍历填完（同一个号出现多次=硬链接，
    // 每个名字都要拿到这份大小）。用定长数组而不是 QHash：几百万条时哈希的常数太大。
    QVector<int> head(int(maxFrn) + 1, -1);
    QVector<int> nxt(want.size(), -1);
    for (int i = 0; i < want.size(); ++i) {
        const int h = int(want[i]);
        nxt[i] = head[h];
        head[h] = i;
    }

    // 开 $MFT 的顺序：① 卷句柄 + 记录号（NTFS 支持的正路；名字那条在提权 + 备份语义下
    //                  都还是 ACCESS_DENIED，2026-10-17 CI 实测）
    //              ② 三种名字写法 × 两种语义（别的 Windows 版本上的退路；每种各记一个错误码）
    HANDLE h = INVALID_HANDLE_VALUE;
    QString openErrs, how;
    {
        VolumeHandle vol;
        if (!vol.open(root)) {
            openErrs += QStringLiteral("卷=%1 ").arg(GetLastError());
        } else if ((h = openMftByRecordId(vol.vol, mftFrn)) == INVALID_HANDLE_VALUE) {
            openErrs += QStringLiteral("按记录号(%1)=0x%2 ")
                            .arg(mftFrn).arg(GetLastError(), 0, 16);
        } else {
            how = QStringLiteral("按记录号开的（frn=0x%1）").arg(mftFrn, 0, 16);
        }
    }
    const std::array<std::wstring, 3> cands = {
        L"\\\\.\\" + drive.toStdWString() + L"\\$MFT",
        drive.toStdWString() + L"\\$MFT",
        L"\\\\.\\" + drive.toStdWString() + L":$MFT"
    };
    for (int round = 0; round < 2 && h == INVALID_HANDLE_VALUE; ++round) {
        DWORD flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN;
        if (round)
            flags |= FILE_FLAG_BACKUP_SEMANTICS;
        {
            // 特权只在这一个花括号里活着
            BackupPrivilege priv;
            if (round && !priv.held()) {
                openErrs += QStringLiteral("备份特权开不上 ");
                break;
            }
            for (const std::wstring& cand : cands) {
                h = CreateFileW(cand.c_str(), FILE_READ_DATA | FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, flags, nullptr);
                if (h != INVALID_HANDLE_VALUE) {
                    how = QStringLiteral("%1（%2）").arg(QString::fromStdWString(cand),
                                                         round ? QStringLiteral("备份语义")
                                                                : QStringLiteral("普通语义"));
                    break;
                }
                openErrs += QStringLiteral("%1=%2 ")
                                .arg(QString::fromStdWString(cand)).arg(GetLastError());
            }
        }
    }
    // ③ 名字和记录号都开不了 → 从卷句柄按簇裸读（这条路完全不碰 $MFT 的 ACL）
    QVector<MftExtent> ext;
    qint64 recBytes = 0;
    HANDLE src = h;
    VolumeHandle vol;
    if (src == INVALID_HANDLE_VALUE) {
        DWORD secPerClus = 0, bytesPerSector = 0;   // 用 DWORD 而不是 quint32：MSVC 上 DWORD 是 unsigned long
        if (!GetDiskFreeSpaceW(reinterpret_cast<LPCWSTR>(root.utf16()), &secPerClus,
                               &bytesPerSector, nullptr, nullptr) || !secPerClus || !bytesPerSector)
            return no(QStringLiteral("$MFT 开不了（%1），连簇大小都问不到（错误码 %2）")
                          .arg(openErrs.trimmed()).arg(GetLastError()));
        if (!vol.open(root))
            return no(QStringLiteral("$MFT 开不了（%1），卷也开不了（错误码 %2）")
                          .arg(openErrs.trimmed()).arg(GetLastError()));
        QString how2;
        if (!mftExtents(vol.vol, qint64(bytesPerSector) * secPerClus, ext, &recBytes, drive, &how2))
            return no(QStringLiteral("$MFT 开不了（%1）· %2").arg(openErrs.trimmed(), how2));
        src = vol.vol;
        how = how2;
    } else {
        LARGE_INTEGER li{};
        GetFileSizeEx(src, &li);
        const qint64 mftBytes = li.QuadPart;
        if (mftBytes < 4096) {
            CloseHandle(h);
            return no(QStringLiteral("$MFT 的大小读不出来（%1 字节）").arg(mftBytes));
        }
        std::vector<BYTE> probe(4 * 4096);
        if (!readAt(src, 0, probe.data(), UINT32(probe.size()))) {
            CloseHandle(h);
            return no(QStringLiteral("$MFT 开头 16KB 都读不到（错误码 %1）").arg(GetLastError()));
        }
        // 记录多长：引导扇区那个字段有版本差异，拿"FILE"签名找周期最稳
        for (const int r : { 256, 512, 1024, 2048, 4096 }) {
            if (3 * r + 4 > int(probe.size()))
                break;
            if (looksLikeFileRecord(probe.data(), r) && looksLikeFileRecord(probe.data() + r, r)
                && looksLikeFileRecord(probe.data() + 2 * r, r)
                && looksLikeFileRecord(probe.data() + 3 * r, r)) {
                recBytes = r;
                break;
            }
        }
        if (!recBytes) {
            CloseHandle(h);
            return no(QStringLiteral("前 16KB 里没找到 FILE 记录的周期（头四格 %1%2%3%4）")
                          .arg(QChar(probe[0])).arg(QChar(probe[1])).arg(QChar(probe[2]))
                          .arg(QChar(probe[3])));
        }
        ext.append({ 0, quint64(mftBytes) });
    }
    if (why)
        *why = QStringLiteral("$MFT 是这么读到的：%1").arg(how);

    // 一条虚拟的连续流：段与段之间记录号接着数（$MFT 这几段本来就是同一个文件的连续内容）
    std::vector<BYTE> chunk(kMftChunk);
    qint64 total = 0;
    for (const MftExtent& e : ext)
        total += qint64(e.bytes) / recBytes;
    const qint64 recsPerChunk = qint64(kMftChunk) / recBytes;
    qint64 seen = 0, filled = 0, nextRec = 0;
    bool cancelled = false;
    for (const MftExtent& e : ext) {
        const qint64 recsInExtent = qint64(e.bytes) / recBytes;
        for (qint64 k = 0; k < recsInExtent; k += recsPerChunk) {
            const int take = int(qMin<qint64>(recsPerChunk, recsInExtent - k));
            if (!readAt(src, e.byteOff + quint64(k) * quint64(recBytes), chunk.data(),
                        UINT32(take * recBytes))) {
                // 读到一半读不动（$MFT 正在长、或卷被别的进程占着）：拿已经拿到的部分去判，
                // 不整趟作废 —— 填上了一半也比全部回退遍历强
                break;
            }
            for (int i = 0; i < take; ++i) {
                const qint64 idx = nextRec + i;
                if (idx > qint64(maxFrn))
                    break;
                int slot = head[int(idx)];
                if (slot < 0)
                    continue;
                qint64 sz = 0;
                if (recordRealSize(chunk.data() + i * recBytes, int(recBytes), &sz)) {
                    for (; slot >= 0; slot = nxt[slot]) {
                        sizes[slot] = sz;
                        ++filled;
                    }
                }
            }
            nextRec += take;
            seen += take;
            if (progress && !progress(seen, total)) {
                cancelled = true;
                break;
            }
            if (filled >= want.size())
                break;
        }
        if (cancelled || filled >= want.size())
            break;
    }
    if (h != INVALID_HANDLE_VALUE)
        CloseHandle(h);
    if (cancelled)
        return no(QStringLiteral("读到一半被取消（已处理 %1 / %2 条记录）").arg(seen).arg(total));
    if (!filled)
        return no(QStringLiteral("$MFT 读了 %1 条记录（共 %2 条、%3 段）却没填上任何一个大小 —— 解析没对上")
                      .arg(seen).arg(total).arg(ext.size()));
    if (why)
        *why = QStringLiteral("%1；读了 %2 / %3 条记录，填上 %4 个大小")
                   .arg(how).arg(seen).arg(total).arg(filled);
    return true;
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

} // namespace UsnIndex
