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
                        const SizeProgress& progress, QString* why)
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

    // $MFT 有三种写法 × 两种语义（普通 / 备份），挨个试。本机非提权下问不出答案（全是 5），
    // 只能让提权的环境去分辨，所以每一种各记一个错误码。备份语义那一轮排在普通那一轮之后：
    // 能不动令牌就不动。
    HANDLE h = INVALID_HANDLE_VALUE;
    QString openErrs;
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
                    if (why)
                        *why = QStringLiteral("$MFT 是这么开到的：%1（%2）")
                                   .arg(QString::fromStdWString(cand),
                                        round ? QStringLiteral("备份语义") : QStringLiteral("普通语义"));
                    break;
                }
                openErrs += QStringLiteral("%1=%2 ")
                                .arg(QString::fromStdWString(cand)).arg(GetLastError());
            }
        }
    }
    if (h == INVALID_HANDLE_VALUE)
        return no(QStringLiteral("$MFT 开不了（%1）").arg(openErrs.trimmed()));

    LARGE_INTEGER li{};
    GetFileSizeEx(h, &li);
    const qint64 mftBytes = li.QuadPart;
    if (mftBytes < 4096) {
        CloseHandle(h);
        return no(QStringLiteral("$MFT 的大小读不出来（%1 字节）").arg(mftBytes));
    }

    // 记录多长：卷引导扇区那个字段有版本差异，直接拿"FILE"签名找周期最稳
    std::vector<BYTE> probeBuf(4 * 4096);
    DWORD got = 0;
    int recBytes = 0;
    if (ReadFile(h, probeBuf.data(), DWORD(probeBuf.size()), &got, nullptr) && got >= 4 * 1024) {
        for (const int r : { 256, 512, 1024, 2048, 4096 }) {
            if (3 * r + 4 > int(got))
                break;
            if (looksLikeFileRecord(probeBuf.data(), r)
                && looksLikeFileRecord(probeBuf.data() + r, r)
                && looksLikeFileRecord(probeBuf.data() + 2 * r, r)
                && looksLikeFileRecord(probeBuf.data() + 3 * r, r)) {
                recBytes = r;
                break;
            }
        }
    }
    if (!recBytes) {
        const DWORD e = GetLastError();
        CloseHandle(h);
        return no(QStringLiteral("前 %1 字节里没找到 FILE 记录的周期（读到 %2 字节，头四格 %3%4%5%6，错误码 %7）")
                      .arg(probeBuf.size()).arg(got)
                      .arg(probeBuf.size() > 0 ? QChar(probeBuf[0]) : QChar())
                      .arg(probeBuf.size() > 1 ? QChar(probeBuf[1]) : QChar())
                      .arg(probeBuf.size() > 2 ? QChar(probeBuf[2]) : QChar())
                      .arg(probeBuf.size() > 3 ? QChar(probeBuf[3]) : QChar()).arg(e));
    }

    std::vector<BYTE> chunk(kMftChunk);
    const qint64 total = mftBytes / recBytes;
    qint64 seen = 0, filled = 0;
    bool cancelled = false;
    for (qint64 base = 0; base + recBytes <= mftBytes; base += qint64(kMftChunk)) {
        LARGE_INTEGER pos{};
        pos.QuadPart = base;
        if (!SetFilePointerEx(h, pos, nullptr, FILE_BEGIN))
            break;
        const DWORD ask = DWORD(qMin<qint64>(kMftChunk, mftBytes - base));
        if (!ReadFile(h, chunk.data(), ask, &got, nullptr) || got < DWORD(recBytes))
            break;                       // $MFT 正在长，尾部读不到就算了
        const int recs = int(got) / recBytes;
        const qint64 first = base / recBytes;
        for (int i = 0; i < recs; ++i) {
            const qint64 idx = first + i;
            if (idx > qint64(maxFrn))
                break;
            int slot = head[int(idx)];
            if (slot >= 0) {
                qint64 sz = 0;
                if (recordRealSize(chunk.data() + i * recBytes, recBytes, &sz)) {
                    for (; slot >= 0; slot = nxt[slot]) {
                        sizes[slot] = sz;
                        ++filled;
                    }
                }
            }
        }
        seen += recs;
        if (progress && !progress(seen, total)) {
            cancelled = true;
            break;
        }
        if (filled >= want.size())
            break;                       // 全拿到了，剩下的记录不必再读
    }
    CloseHandle(h);
    if (cancelled)
        return no(QStringLiteral("读到一半被取消（已处理 %1 / %2 条记录）").arg(seen).arg(total));
    if (!filled)
        return no(QStringLiteral("$MFT 读了 %1 条记录（共 %2）却没填上任何一个大小 —— 解析没对上")
                      .arg(seen).arg(total));
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
