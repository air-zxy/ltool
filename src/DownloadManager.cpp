#include "DownloadManager.h"
#include "DevEnv.h"
#include "Utils.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QUuid>

#include <algorithm>

namespace {

QNetworkAccessManager* sharedNam()
{
    static QNetworkAccessManager nam;
    DevEnv::applyProxy(&nam);   // 每次取用都重读一遍：配置页刚改的代理立刻生效
    return &nam;
}

QString downloadsFile() { return Utils::dataDir() + "/downloads.json"; }

} // namespace

DownloadManager& DownloadManager::instance()
{
    static DownloadManager inst;
    return inst;
}

qint64 DownloadTask::receivedTotal() const
{
    qint64 n = 0;
    for (const DownloadPart& p : parts)
        n += p.received;
    return n;
}

DownloadTask* DownloadManager::taskById(const QString& id)
{
    for (DownloadTask& t : m_tasks)
        if (t.id == id)
            return &t;
    return nullptr;
}

void DownloadManager::start()
{
    load();
}

void DownloadManager::load()
{
    QFile f(downloadsFile());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const auto& v : arr) {
        const QJsonObject o = v.toObject();
        DownloadTask t;
        t.id = o.value("id").toString();
        t.url = o.value("url").toString();
        t.file = o.value("file").toString();
        t.total = o.value("total").toVariant().toLongLong();
        t.acceptRanges = o.value("ranges").toBool(false);
        t.concurrency = o.value("concurrency").toInt(6);
        t.status = o.value("status").toString();
        if (t.status == "下载中")
            t.status = "已暂停"; // 重启后不自动续传
        for (const auto& pv : o.value("parts").toArray()) {
            const QJsonObject po = pv.toObject();
            DownloadPart part;
            part.start = po.value("start").toVariant().toLongLong();
            part.end = po.value("end").toVariant().toLongLong();
            part.received = po.value("received").toVariant().toLongLong();
            part.done = po.value("done").toBool();
            t.parts.append(part);
        }
        if (!t.url.isEmpty() && !t.file.isEmpty())
            m_tasks.append(t);
    }
}

void DownloadManager::save()
{
    QJsonArray arr;
    for (const DownloadTask& t : m_tasks) {
        QJsonObject o;
        o.insert("id", t.id);
        o.insert("url", t.url);
        o.insert("file", t.file);
        o.insert("total", QString::number(t.total));
        o.insert("ranges", t.acceptRanges);
        o.insert("concurrency", t.concurrency);
        o.insert("status", t.status);
        if (!t.error.isEmpty())
            o.insert("error", t.error);
        QJsonArray parts;
        for (const DownloadPart& p : t.parts) {
            QJsonObject po;
            po.insert("start", QString::number(p.start));
            po.insert("end", QString::number(p.end));
            po.insert("received", QString::number(p.received));
            po.insert("done", p.done);
            parts.append(po);
        }
        o.insert("parts", parts);
        arr.append(o);
    }
    QFile f(downloadsFile());
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
}

QString DownloadManager::saveDir() const
{
    // 走 DevEnv::installLocation()：配置的盘符不存在时同样退到 C 盘
    QString dir = DevEnv::installLocation();
    if (dir.isEmpty() || !QDir(dir).exists())
        dir = QDir::homePath() + "/Downloads";
    return dir;
}

void DownloadManager::add(const QString& url, const QString& savePath)
{
    DownloadTask t;
    t.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    t.url = url;
    t.file = savePath;
    t.status = "等待";
    m_tasks.append(t);
    save();
    startTask(t.id);
    emit changed();
}

void DownloadManager::startTask(const QString& id)
{
    DownloadTask* t = taskById(id);
    if (!t)
        return;
    t->error.clear();
    t->status = "下载中";
    emit changed();

    // HEAD 探测：大小 + Range 支持
    QNetworkRequest req(t->url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* probe = sharedNam()->head(req);
    QEventLoop loop;
    QObject::connect(probe, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (probe->error() != QNetworkReply::NoError) {
        t->status = "失败";
        t->error = probe->errorString();
        probe->deleteLater();
        save();
        emit changed();
        return;
    }
    const QVariant lenHdr = probe->header(QNetworkRequest::ContentLengthHeader);
    t->total = lenHdr.isValid() ? lenHdr.toLongLong() : -1;
    const QString accept = QString::fromLatin1(probe->rawHeader("Accept-Ranges"));
    t->acceptRanges = (accept.compare("bytes", Qt::CaseInsensitive) == 0) && t->total > 0;
    probe->deleteLater();

    if (!t->fileHandle)
        t->fileHandle = new QFile(t->file);
    QDir().mkpath(QFileInfo(t->file).absolutePath());
    if (!t->fileHandle->isOpen() && !t->fileHandle->open(QIODevice::ReadWrite)) {
        t->status = "失败";
        t->error = "无法创建文件";
        save();
        emit changed();
        return;
    }
    if (t->total > 0)
        t->fileHandle->resize(t->total);

    if (t->parts.isEmpty()) {
        if (t->acceptRanges) {
            const int n = t->concurrency;
            const qint64 chunk = t->total / n;
            for (int i = 0; i < n; ++i) {
                DownloadPart p;
                p.start = qint64(i) * chunk;
                p.end = (i == n - 1) ? t->total - 1 : p.start + chunk - 1;
                t->parts.append(p);
            }
        } else {
            DownloadPart p;
            p.end = t->total > 0 ? t->total - 1 : -1;
            t->parts.append(p);
        }
    }

    for (int i = 0; i < t->parts.size(); ++i)
        startPart(id, i);
}

void DownloadManager::startPart(const QString& id, int index)
{
    DownloadTask* t = taskById(id);
    if (!t || t->status != "下载中")
        return;
    DownloadPart& part = t->parts[index];
    if (part.done)
        return;
    const qint64 from = part.start + part.received;
    QNetworkRequest req(t->url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    if (t->acceptRanges)
        req.setRawHeader("Range",
                         QString("bytes=%1-%2").arg(from).arg(part.end).toLatin1());
    QNetworkReply* reply = sharedNam()->get(req);
    part.reply = reply;
    const bool ranges = t->acceptRanges;

    QObject::connect(reply, &QNetworkReply::readyRead, this, [this, id, index, reply] {
        DownloadTask* t = taskById(id);
        if (!t || !t->fileHandle)
            return;
        DownloadPart& part = t->parts[index];
        const QByteArray data = reply->readAll();
        if (data.isEmpty())
            return;
        t->fileHandle->seek(part.start + part.received);
        const qint64 n = t->fileHandle->write(data);
        part.received += n;
        emit changed();
    });
    QObject::connect(reply, &QNetworkReply::finished, this, [this, id, index, ranges, reply] {
        reply->deleteLater();
        DownloadTask* t = taskById(id);
        if (!t)
            return;
        DownloadPart& part = t->parts[index];
        part.reply = nullptr;
        if (t->status != "下载中") {
            emit changed();
            return; // 已暂停/移除
        }
        const qint64 expected = part.end - part.start + 1;
        const bool complete = ranges ? part.received >= expected
                                     : (t->total < 0 || part.received >= t->total);
        if (!complete) {
            startPart(id, index); // 断点续传
            return;
        }
        part.done = true;
        emit changed();
        taskTick(id);
    });
    QObject::connect(reply, &QNetworkReply::errorOccurred, this, [this, id] {
        DownloadTask* t = taskById(id);
        if (t && t->status == "下载中") {
            t->status = "失败";
            t->error = "网络错误";
            save();
            emit changed();
        }
    });
}

void DownloadManager::taskTick(const QString& id)
{
    DownloadTask* t = taskById(id);
    if (!t)
        return;
    for (const DownloadPart& p : t->parts) {
        if (!p.done)
            return; // 还有分块未完成
    }
    if (t->fileHandle) {
        t->fileHandle->close();
        delete t->fileHandle;
        t->fileHandle = nullptr;
    }
    t->status = "完成";
    Utils::log(QString("download finished: %1").arg(t->file));
    save();
    emit changed();
}

void DownloadManager::pause(const QString& id)
{
    DownloadTask* t = taskById(id);
    if (!t)
        return;
    t->status = "已暂停"; // 先改状态，abort 后 finished 不再续传
    for (DownloadPart& p : t->parts) {
        if (p.reply) {
            p.reply->abort();
            p.reply = nullptr;
        }
    }
    save();
    emit changed();
}

void DownloadManager::resume(const QString& id)
{
    DownloadTask* t = taskById(id);
    if (!t || t->status == "完成" || t->status == "下载中")
        return;
    if (!t->fileHandle) {
        t->fileHandle = new QFile(t->file);
        if (!t->fileHandle->isOpen() && !t->fileHandle->open(QIODevice::ReadWrite)) {
            t->status = "失败";
            t->error = "无法打开文件";
            emit changed();
            return;
        }
    }
    t->status = "下载中";
    emit changed();
    for (int i = 0; i < t->parts.size(); ++i)
        startPart(id, i);
}

void DownloadManager::remove(const QString& id)
{
    DownloadTask* t = taskById(id);
    if (!t)
        return;
    if (t->status == "下载中")
        pause(id);
    if (t->fileHandle) {
        t->fileHandle->close();
        delete t->fileHandle;
        t->fileHandle = nullptr;
    }
    const int idx = int(std::distance(m_tasks.begin(),
                                      std::find_if(m_tasks.begin(), m_tasks.end(),
                                                   [&](const DownloadTask& x) { return x.id == id; })));
    if (idx >= 0 && idx < m_tasks.size())
        m_tasks.removeAt(idx);
    save();
    emit changed();
}
