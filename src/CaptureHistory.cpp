#include "CaptureHistory.h"

#include "Utils.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfoList>

namespace {

constexpr int kMaxHistory = 12;   // 左侧缩略图列出来的张数
constexpr int kMaxFiles = 200;    // captures 目录最多留多少张，超了删最旧的

bool g_histLoaded = false;

// 注意这是**一个**变量：写成两个函数各自的函数局部 static，就是两份状态，
// 插件写了 core 永远读到 false（搜索框会在浮层举着的时候走掉）。
bool g_captureActive = false;

} // namespace

QVector<CaptureHistory::Entry>& CaptureHistory::items()
{
    static QVector<Entry> v;
    return v;
}

QString CaptureHistory::dir()
{
    return Utils::capturesDir();
}

// trim=false 用在"配置页刚换了目录"那一次：那个目录里可能本来就有你自己的文件，
// 我们不该一进去就把超出配额的那些删掉（启动时的那次照旧清理）
void CaptureHistory::load(bool trim)
{
    if (g_histLoaded)
        return;
    g_histLoaded = true;
    QDir d(dir());
    if (!d.exists())
        return;
    // 按时间从新到旧读回最近几档：置顶窗开着的正是"往期截图"，重启后也要看得见
    const QFileInfoList files = d.entryInfoList({QStringLiteral("cap-*.png")}, QDir::Files, QDir::Time);
    for (int i = 0; i < files.size(); ++i) {
        if (items().size() >= kMaxHistory)
            break;
        QPixmap pm;
        if (pm.load(files[i].absoluteFilePath()))
            items().append({pm, files[i].fileName()});
    }
    if (!trim)
        return;
    for (int i = kMaxFiles; i < files.size(); ++i)
        QFile::remove(files[i].absoluteFilePath());
}

void CaptureHistory::reload()
{
    g_histLoaded = false;
    items().clear();
    load(false);
}

QString CaptureHistory::add(const QPixmap& pm)
{
    load();
    if (pm.isNull())
        return {};
    auto& v = items();
    // 只有"和最新一张逐像素完全一样"才算重复。以前是拿左上角 8×8 当指纹，
    // 于是同一块区域再截一次就被丢掉——表现就是"截图置顶了，左边看不见往期截图"。
    if (!v.isEmpty() && v.first().pm.size() == pm.size()
        && v.first().pm.toImage() == pm.toImage())
        return v.first().key;
    QDir().mkpath(dir());
    static int seq = 0;
    const QString key = QString("cap-%1-%2.png")
                            .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"))
                            .arg(++seq, 3, 10, QLatin1Char('0'));
    Entry e;
    e.pm = pm;
    e.key = pm.save(dir() + "/" + key) ? key : QString();
    v.prepend(e);
    while (v.size() > kMaxHistory)
        v.removeLast();
    return e.key;
}

void CaptureHistory::replace(const QString& key, const QPixmap& pm, bool write)
{
    if (key.isEmpty() || pm.isNull())
        return;
    load();
    for (Entry& e : items()) {
        if (e.key != key)
            continue;
        e.pm = pm;
        if (write)
            pm.save(dir() + "/" + key);   // 编辑结果也落盘，下次开还在
        return;
    }
}

int CaptureHistory::count()
{
    load();
    return items().size();
}

CaptureHistory::Item CaptureHistory::at(int index)
{
    load();
    const auto& v = items();
    if (index < 0 || index >= v.size())
        return {};
    return {v[index].pm, v[index].key};
}

QString CaptureHistory::removeAt(int index)
{
    load();
    auto& v = items();
    if (index < 0 || index >= v.size())
        return {};
    const QString key = v[index].key;
    v.removeAt(index);
    if (!key.isEmpty())
        QFile::remove(dir() + "/" + key);   // 往期就是那个文件，不删的话下次启动又被读回来
    return key;
}

void CaptureHistory::clear()
{
    load();
    for (const Entry& e : items()) {
        if (!e.key.isEmpty())
            QFile::remove(dir() + "/" + e.key);
    }
    items().clear();
}

void CaptureState::setActive(bool on)
{
    g_captureActive = on;
}

bool CaptureState::isActive()
{
    return g_captureActive;
}
