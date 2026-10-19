#include "ClipboardStore.h"
#include "Utils.h"

#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QSettings>
#include <QSet>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <algorithm>

#include <windows.h>   // GetClipboardSequenceNumber：POSIX 没有对等计数器（#90 第五件）

namespace {

QString configPath() { return Utils::dataDir() + "/config.ini"; }
QString storePath() { return Utils::dataDir() + "/clipboard.json"; }
QString imageDir() { return Utils::dataDir() + "/clipimg"; }

QString human(qint64 bytes)
{
    const double kb = bytes / 1024.0;
    if (kb < 1024)
        return QString("%1 KB").arg(kb, 0, 'f', kb < 10 ? 1 : 0);
    return QString("%1 MB").arg(kb / 1024.0, 0, 'f', 1);
}

} // namespace

ClipboardStore& ClipboardStore::instance()
{
    static ClipboardStore s;
    return s;
}

void ClipboardStore::start()
{
    if (m_started)
        return;
    m_started = true;
    load();
    QDir().mkpath(imageDir());
    // dataChanged 在部分程序写剪贴板时不发（尤其带延迟渲染的），再加一个 2 秒兜底轮询。
    // 轮询只问一次 GetClipboardSequenceNumber()：Windows 维护的计数器，剪贴板没变就几微秒返回。
    // 不加这道闸门时，一张 2560x1440 截图躺在剪贴板上会让每 2 秒重读+重编码一次，
    // 实测稳定烧掉 13.8% 单核（4125ms/30s），而且全在 GUI 线程上。
    //
    // 这一路**必须推到下一个事件循环回合**，不能在信号里直接 capture()：我们自己的
    // restore() 往剪贴板里写的那一下也会同步发 dataChanged，而那时 m_seq 还没更新
    // （序号是写完才变的），于是同一线程里立刻回头读自己刚写的剪贴板、再给历史做一次
    // 重编码 —— 图片那条实测要几百毫秒（他看到的"点一下卡住"），更要紧的是它会在
    // restore() 还拿着 m_items 引用时就把这个 vector 挪走。
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this] {
        QTimer::singleShot(0, this, [this] {
            const UINT s = GetClipboardSequenceNumber();
            if (s == m_seq)
                return;                 // 自己写的那一下（restore 已经把 m_seq 追平了），或已经收过
            m_seq = s;
            capture();
        });
    });
    auto* poll = new QTimer(qApp);
    poll->setInterval(2000);
    QObject::connect(poll, &QTimer::timeout, poll, [this] {
        // 按天裁剪一天只做一次就够了：常驻实例可能连着开好几天，或者收集本来就是关着的
        // （那样 capture() 第一行就返回，永远走不到 prune()）—— 两种情况下超龄的也得走。
        if (m_sweptOn != QDate::currentDate()) {
            m_sweptOn = QDate::currentDate();
            const int before = m_items.size();
            prune();
            if (m_items.size() != before)
                save();
        }
        const UINT seq = GetClipboardSequenceNumber();
        if (seq == m_seq)
            return;
        m_seq = seq;
        capture();
    });
    poll->start();
    Utils::log(QString("clipboard: started enabled=%1 items=%2").arg(m_enabled).arg(m_items.size()));
}

void ClipboardStore::load()
{
    QSettings cfg(configPath(), QSettings::IniFormat);
    m_enabled = cfg.value("clipboard/enable", false).toBool();
    m_keepDays = cfg.value("clipboard/keepDays", kDefaultKeepDays).toInt();
    if (m_keepDays < 0)
        m_keepDays = 0;
    m_sweptOn = QDate::currentDate();     // 这次 load() 就是一次按天裁剪，别紧接着再扫一遍

    m_items.clear();
    QFile f(storePath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        Item it;
        it.when = QDateTime::fromString(o.value("when").toString(), Qt::ISODate);
        it.hash = o.value("hash").toString();
        it.isImage = o.value("image").toBool();
        it.text = o.value("text").toString();
        it.imagePath = o.value("path").toString();
        it.w = o.value("w").toInt();
        it.h = o.value("h").toInt();
        it.bytes = qint64(o.value("bytes").toDouble());
        if (!it.isImage && it.text.isEmpty())
            continue;
        if (it.isImage && !QFileInfo::exists(it.imagePath))
            continue;                       // 图被删了就不留空条目
        m_items.append(it);
        if (m_items.size() >= kMaxItems)
            break;
    }
    // 超龄的先裁掉（老版本只按条数封顶，所以第一次带上天数规则启动时，
    // 这一步真的会删东西 —— 删完就把 json 重写，别让同一批条目每次启动都重算一遍）。
    const int before = m_items.size();
    prune();
    if (m_items.size() != before) {
        save();
        Utils::log(QString("clipboard: pruned on load %1 -> %2 (keepDays=%3)")
                       .arg(before).arg(m_items.size()).arg(m_keepDays));
    }
}

void ClipboardStore::save() const
{
    QJsonArray arr;
    for (const Item& it : m_items) {
        QJsonObject o;
        o.insert("when", it.when.toString(Qt::ISODate));
        o.insert("hash", it.hash);
        o.insert("image", it.isImage);
        o.insert("bytes", double(it.bytes));
        if (it.isImage) {
            o.insert("path", it.imagePath);
            o.insert("w", it.w);
            o.insert("h", it.h);
        } else {
            o.insert("text", it.text);
        }
        arr.append(o);
    }
    QFile f(storePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void ClipboardStore::setEnabled(bool on)
{
    if (m_enabled == on)
        return;
    m_enabled = on;
    QSettings cfg(configPath(), QSettings::IniFormat);
    cfg.setValue("clipboard/enable", on);
    Utils::log(QString("clipboard: enabled=%1").arg(on));
    if (on)
        capture();     // 刚打开就把当前内容收进去，别等下一次复制
}

bool ClipboardStore::expired(const Item& it, int keepDays, const QDateTime& now)
{
    if (keepDays <= 0)
        return false;
    if (!it.when.isValid())
        return true;
    return it.when.secsTo(now) >= qint64(keepDays) * 86400;
}

void ClipboardStore::setKeepDays(int days)
{
    const int clamped = days < 0 ? 0 : days;
    QSettings cfg(configPath(), QSettings::IniFormat);
    cfg.setValue("clipboard/keepDays", clamped);
    if (clamped == m_keepDays)
        return;
    m_keepDays = clamped;
    const int before = m_items.size();
    prune();
    save();
    Utils::log(QString("clipboard: keepDays=%1 items %2 -> %3")
                   .arg(m_keepDays).arg(before).arg(m_items.size()));
}

void ClipboardStore::capture()
{
    if (!m_enabled)
        return;
    const QMimeData* mime = QApplication::clipboard()->mimeData();
    if (!mime)
        return;

    Item it;
    it.when = QDateTime::currentDateTime();

    QImage src = qvariant_cast<QImage>(mime->imageData());
    if (!src.isNull()) {
        // 去重的指纹用规范化后的原始位（一次格式转换 + 哈希，十几 ms 量级）。
        // 之前是"先 PNG 编码再哈希"：一张 2560x1440 截图编码要 ~400ms，白花在重复内容上。
        static constexpr qint64 kHashCap = 64LL * 1024 * 1024;
        const QImage canon = src.convertToFormat(QImage::Format_ARGB32);
        const qint64 bitsLen = qMin(qint64(canon.bytesPerLine()) * canon.height(), kHashCap);
        it.hash = QString::fromLatin1(
            QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char*>(canon.constBits()),
                                                    qsizetype(bitsLen)),
                                     QCryptographicHash::Sha1).toHex());
        if (!m_items.isEmpty() && m_items.first().hash == it.hash) {
            m_lastHash = it.hash;
            return;                       // 同一张图反复被通知，别刷屏
        }
        QByteArray raw;
        {
            QBuffer b(&raw);
            b.open(QIODevice::WriteOnly);
            canon.save(&b, "PNG");
        }
        it.isImage = true;
        it.w = canon.width();
        it.h = canon.height();
        it.bytes = raw.size();
        it.imagePath = imageDir() + "/" + it.hash + ".png";
        if (!QFileInfo::exists(it.imagePath)) {
            QFile f(it.imagePath);
            if (!f.open(QIODevice::WriteOnly))
                return;
            f.write(raw);
            f.close();
            // 收进来的这一刻顺手落一张 40px 的：那张页以后每一行都只读这张小的，
            // 不该为了"你刚截的那张 2560×1440"在 GUI 线程上等一次整图解码。
            canon.scaled(40, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                .save(thumbPath(it.imagePath), "PNG");
        }
    } else if (mime->hasText()) {
        const QString text = mime->text();
        if (text.trimmed().isEmpty())
            return;
        const QByteArray utf8 = text.toUtf8();
        if (utf8.size() > kMaxTextBytes)
            return;                       // 大段日志不值得占内存和磁盘
        it.hash = QString::fromLatin1(
            QCryptographicHash::hash(utf8, QCryptographicHash::Sha1).toHex());
        it.text = text;
        it.bytes = utf8.size();
    } else {
        return;                           // 文件列表之类先不收
    }

    if (it.hash == m_lastHash && !m_items.isEmpty() && m_items.first().hash == it.hash)
        return;
    m_lastHash = it.hash;

    const int dup = std::find_if(m_items.begin(), m_items.end(),
                                 [&](const Item& x) { return x.hash == it.hash; })
                      - m_items.begin();
    if (dup < m_items.size())
        m_items.removeAt(dup);            // 去重：同一内容重新置顶
    m_items.prepend(it);
    prune();
    save();
}

bool ClipboardStore::sweepAllowed(int itemCount)
{
    // 一条历史都没 load 到 ≠ 盘上那些图都不需要了。真要全删走的是 clear() 自己那一段，
    // 不该由"读不到条目"这件事顺带触发。
    return itemCount > 0;
}

QString ClipboardStore::keepKey(const QString& path)
{
    // 比较两侧都要归一化 + 折叠大小写：json 里那条路径的写法不保证是正斜杠（APPDATA 本身
    // 就是反斜杠，拼出来是混着的），而 entryList 给的名字要自己拼回去才算路径。
    // 2026-10-16 那次只归一化了一边，两边永远不相等，启动那一趟把 49 张还在用的图
    // 全判成孤儿删了 —— 这就是"清垃圾"那段自己变成凶手的形状。
    QString s = QDir::fromNativeSeparators(path);
    s.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (s.contains(QStringLiteral("//")))
        s.replace(QStringLiteral("//"), QStringLiteral("/"));
    return s.toLower();
}

void ClipboardStore::prune()
{
    // 两条上限取交集：先按天裁，再按条数封顶。列表不保证严格按时间排（重复内容会被
    // 重新置顶），所以逐条问，不敢假设超龄的都堆在尾巴上。
    if (m_keepDays > 0) {
        const QDateTime now = QDateTime::currentDateTime();
        for (int i = m_items.size() - 1; i >= 0; --i)
            if (expired(m_items.at(i), m_keepDays, now))
                m_items.removeAt(i);
    }
    while (m_items.size() > kMaxItems)
        m_items.removeLast();

    if (!sweepAllowed(static_cast<int>(m_items.size())))
        return;
    QSet<QString> keep;
    for (const Item& it : m_items)
        if (it.isImage) {
            keep.insert(keepKey(it.imagePath));
            keep.insert(keepKey(thumbPath(it.imagePath)));   // 缩略图跟着原图活：原图在，它就不算孤儿
        }
    int wiped = 0;
    const auto files = QDir(imageDir()).entryList(QStringList() << QStringLiteral("*.png"), QDir::Files);
    for (const QString& name : files) {
        const QString full = imageDir() + "/" + name;
        if (!keep.contains(keepKey(full))) {
            QFile::remove(full);
            ++wiped;
        }
    }
    if (wiped > 0)     // 批量删文件必须留一行日志：不写的话这件事永远不会有人知道
        Utils::log(QString("clipboard: swept %1 orphan image file(s), items=%2 keepDays=%3")
                       .arg(wiped).arg(m_items.size()).arg(m_keepDays));
}

void ClipboardStore::restore(int index)
{
    if (index < 0 || index >= m_items.size())
        return;
    // 拿**副本**，不拿引用：往剪贴板里写这一步会同步发出 dataChanged，我们的槽就在同一个
    // 线程里跑 capture() —— 它把这一条挪到最前（先 removeAt 再 prepend），m_items 就此搬家，
    // 引用会指向已经释放的内存，而最后那行日志还在读它的字段。2026-10-16 他那次闪退
    // （异常地址落在 Qt6Core.dll 里）最符合这个形状。
    const Item it = m_items[index];
    QClipboard* cb = QApplication::clipboard();
    if (it.isImage) {
        const QImage img(it.imagePath);
        if (img.isNull())
            return;
        m_lastHash = it.hash;
        cb->setImage(img);
    } else {
        m_lastHash = it.hash;
        cb->setText(it.text);
    }
    m_seq = GetClipboardSequenceNumber();   // 自己写的不算"用户复制了新东西"
    save();
    Utils::log(QString("clipboard: restored %1 (%2)").arg(index).arg(it.isImage ? "image" : "text"));
}

void ClipboardStore::remove(int index)
{
    if (index < 0 || index >= m_items.size())
        return;
    m_items.removeAt(index);
    prune();
    save();
}

void ClipboardStore::clear()
{
    m_items.clear();
    m_lastHash.clear();
    const auto files = QDir(imageDir()).entryList(QStringList() << QStringLiteral("*.png"), QDir::Files);
    for (const QString& name : files)
        QFile::remove(imageDir() + "/" + name);
    save();
    Utils::log("clipboard: history cleared");
}

QString ClipboardStore::thumbPath(const QString& imagePath)
{
    // clipimg/<sha1>.png -> clipimg/<sha1>.t120.png：和原图同一个目录、同一次清理，
    // 不另开一棵目录树（开一棵就得再写一遍"孤儿怎么撤"，两处迟早对不上）。
    const QFileInfo fi(imagePath);
    return fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(".t120.png");
}

// 卡片上那块图最多 120×84（2026-10-19 他把这张列表改成卡片之后，40px 那颗就不够看了）。
constexpr int kThumbW = 120;
constexpr int kThumbH = 84;

QIcon ClipboardStore::iconFor(const Item& it) const
{
    if (!it.isImage)
        return QIcon();
    const QString thumb = thumbPath(it.imagePath);
    // 这张页每敲一个过滤字、每次被看见都要把列表重铺一遍 —— 原来那是 51 张整图重解码（实测 632ms）。
    // 键用路径而不是 hash：路径本来就由 hash 派生，多一个键源只会多一处不一致。
    static QHash<QString, QIcon> cache;
    if (auto hit = cache.constFind(thumb); hit != cache.constEnd())
        return hit.value();
    QPixmap pm(thumb);
    if (pm.isNull()) {   // 没有缩略图（老历史里的第一批）：拿原图做一次，落盘，之后只读这张小的
        QPixmap full(it.imagePath);
        if (full.isNull())
            return QIcon();
        pm = full.scaled(kThumbW, kThumbH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        pm.save(thumb, "PNG");
    }
    const QIcon icon(pm);
    cache.insert(thumb, icon);
    return icon;
}

QString ClipboardStore::summary(const Item& it)
{
    if (it.isImage)
        return QString("图片 %1×%2 · %3").arg(it.w).arg(it.h).arg(human(it.bytes));
    const QString first = it.text.section(QLatin1Char('\n'), 0, 0).trimmed();
    const int lines = it.text.count(QLatin1Char('\n')) + 1;
    QString shown = first;
    if (shown.size() > 70)
        shown = shown.left(70) + QStringLiteral("…");
    if (shown.isEmpty())
        shown = QStringLiteral("(空白)");
    return lines > 1 ? QString("%1  · %2 行 · %3").arg(shown).arg(lines).arg(human(it.bytes))
                     : QString("%1  · %2").arg(shown).arg(human(it.bytes));
}
