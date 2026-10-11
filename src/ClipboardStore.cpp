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

#include <windows.h>

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
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this] {
        m_seq = GetClipboardSequenceNumber();
        capture();
    });
    auto* poll = new QTimer(qApp);
    poll->setInterval(2000);
    QObject::connect(poll, &QTimer::timeout, poll, [this] {
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

void ClipboardStore::prune()
{
    while (m_items.size() > kMaxItems)
        m_items.removeLast();
    QSet<QString> keep;
    for (const Item& it : m_items)
        if (it.isImage)
            keep.insert(it.imagePath);
    const auto files = QDir(imageDir()).entryList(QStringList() << QStringLiteral("*.png"), QDir::Files);
    for (const QString& name : files) {
        const QString full = imageDir() + "/" + name;
        if (!keep.contains(full))
            QFile::remove(full);
    }
}

void ClipboardStore::restore(int index)
{
    if (index < 0 || index >= m_items.size())
        return;
    const Item& it = m_items[index];
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

QIcon ClipboardStore::iconFor(const Item& it) const
{
    if (!it.isImage)
        return QIcon();
    QPixmap pm(it.imagePath);
    if (pm.isNull())
        return QIcon();
    return QIcon(pm.scaled(40, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation));
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
