#include "AdBlocker.h"

#include "Utils.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTimer>

#include <shellscalingapi.h>

#include <algorithm>

namespace {

// 默认名单：进程名片段。宁可短而准，误杀一个正常窗口比漏掉十个弹窗更让人恼火。
const char* kDefaultProc[] = {
    "wpsnotify", "wpscenter", "wpscloudsvr", "kpluginhost", "kingsoft_ad",
    "kuaikan", "hotnews", "newsfeed", "pushad", "adpopup", "billgather",
    "tscenter", "newsapp", "miniworld", "roshtask", "updates"   // 各家 updater 的推送壳
};
const char* kDefaultTitle[] = {
    "今日热点", "热点资讯", "新闻资讯", "每日推荐", "限时福利", "红包", "优惠券",
    "弹窗通知", "推广", "促销活动", "升级提醒", "已为您", "惊喜"
};

constexpr int kHistoryKeep = 1000;
// 同一个窗口没关掉时，5 秒一轮会反复命中；这个间隔内只记一次，否则统计会被同一个弹窗灌满
constexpr qint64 kRecordDedupeMs = 60 * 1000;

QString configPath() { return Utils::dataDir() + "/config.ini"; }
QString rulesPath() { return Utils::dataDir() + "/adblock.json"; }
QString historyPath() { return Utils::dataDir() + "/adhistory.json"; }

BOOL CALLBACK enumOne(HWND hwnd, LPARAM lParam)
{
    auto* out = reinterpret_cast<QVector<AdBlocker::Hit>*>(lParam);
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd))
        return TRUE;
    wchar_t cls[64];
    GetClassNameW(hwnd, cls, 64);
    const QString className = QString::fromWCharArray(cls);
    if (className == QLatin1String("Progman") || className == QLatin1String("WorkerW")
        || className == QLatin1String("Shell_TrayWnd")
        || className == QLatin1String("Windows.UI.Core.CoreWindow"))
        return TRUE;
    wchar_t title[512];
    const int len = GetWindowTextW(hwnd, title, 512);
    if (len < 2)
        return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    const QString proc = AdBlocker::exeNameOfPid(pid);
    const QString t = QString::fromWCharArray(title, len);
    if (proc.compare(QLatin1String("ltool.exe"), Qt::CaseInsensitive) == 0)
        return TRUE;

    RECT rc;
    GetWindowRect(hwnd, &rc);
    AdBlocker::Hit h;
    h.hwnd = hwnd;
    h.title = t;
    h.process = proc;
    h.pid = pid;
    h.width = rc.right - rc.left;
    h.height = rc.bottom - rc.top;
    out->append(h);
    return TRUE;
}

} // namespace

AdBlocker& AdBlocker::instance()
{
    static AdBlocker s;
    return s;
}

QString AdBlocker::exeNameOfPid(DWORD pid)
{
    QString name;
    if (HANDLE ph = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        WCHAR buf[1024];
        DWORD len = 1024;
        if (QueryFullProcessImageNameW(ph, 0, buf, &len))
            name = QFileInfo(QString::fromWCharArray(buf, int(len))).fileName();
        CloseHandle(ph);
    }
    return name;
}

void AdBlocker::load()
{
    QSettings cfg(configPath(), QSettings::IniFormat);
    m_auto = cfg.value("adblock/auto", false).toBool();
    m_observe = cfg.value("adblock/observe", false).toBool();

    m_proc.clear();
    m_title.clear();
    m_allowProc.clear();
    m_allowTitle.clear();
    QFile f(rulesPath());
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        m_proc = o.value("proc").toVariant().toStringList();
        m_title = o.value("title").toVariant().toStringList();
        m_allowProc = o.value("allow_proc").toVariant().toStringList();
        m_allowTitle = o.value("allow_title").toVariant().toStringList();
    }
    if (m_proc.isEmpty())
        m_proc = defaultProcPatterns();
    if (m_title.isEmpty())
        m_title = defaultTitlePatterns();
    loadHistory();
}

QStringList AdBlocker::defaultProcPatterns()
{
    QStringList out;
    for (const char* p : kDefaultProc)
        out << QString::fromLatin1(p);
    return out;
}

QStringList AdBlocker::defaultTitlePatterns()
{
    QStringList out;
    for (const char* p : kDefaultTitle)
        out << QString::fromUtf8(p);
    return out;
}

void AdBlocker::save() const
{
    QSettings cfg(configPath(), QSettings::IniFormat);
    cfg.setValue("adblock/auto", m_auto);
    cfg.setValue("adblock/observe", m_observe);

    QJsonObject o;
    QJsonArray pa, ta, apa, ata;
    for (const QString& p : m_proc)
        pa.append(p);
    for (const QString& t : m_title)
        ta.append(t);
    for (const QString& p : m_allowProc)
        apa.append(p);
    for (const QString& t : m_allowTitle)
        ata.append(t);
    o.insert("proc", pa);
    o.insert("title", ta);
    o.insert("allow_proc", apa);
    o.insert("allow_title", ata);
    QFile f(rulesPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
}

// 记录落盘：不然每次重启"今天已关 N 个"都从 0 开始，看不出这东西到底拦没拦
void AdBlocker::loadHistory()
{
    m_history.clear();
    QFile f(historyPath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        Record r;
        r.when = QDateTime::fromString(o.value("when").toString(), Qt::ISODate);
        r.title = o.value("title").toString();
        r.process = o.value("process").toString();
        r.pid = quint32(o.value("pid").toDouble());
        r.closed = o.value("closed").toBool();
        if (r.when.isValid())
            m_history.append(r);
    }
    while (m_history.size() > kHistoryKeep)
        m_history.removeFirst();
}

void AdBlocker::saveHistory() const
{
    QJsonArray arr;
    for (const Record& r : m_history) {
        QJsonObject o;
        o.insert("when", r.when.toString(Qt::ISODate));
        o.insert("title", r.title);
        o.insert("process", r.process);
        o.insert("pid", double(r.pid));
        o.insert("closed", r.closed);
        arr.append(o);
    }
    QFile f(historyPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void AdBlocker::start()
{
    if (m_started)
        return;
    m_started = true;
    load();
    // 定时器挂在 qApp 上：设置页关掉也继续拦，这才是"常驻拦截器"
    auto* t = new QTimer(qApp);
    t->setInterval(5000);
    QObject::connect(t, &QTimer::timeout, t, [this] { tick(); });
    t->start();
    Utils::log(QString("adblock: started auto=%1 observe=%2 proc=%3 title=%4")
                   .arg(m_auto).arg(m_observe).arg(m_proc.size()).arg(m_title.size()));
}

QStringList AdBlocker::procPatterns() const
{
    QStringList copy = m_proc;
    copy.removeDuplicates();
    return copy;
}

QStringList AdBlocker::titlePatterns() const
{
    QStringList copy = m_title;
    copy.removeDuplicates();
    return copy;
}

bool AdBlocker::matches(const QString& title, const QString& process) const
{
    if (allowed(title, process))
        return false;
    const QString p = process.toLower();
    for (const QString& pat : m_proc)
        if (!pat.isEmpty() && p.contains(pat, Qt::CaseInsensitive))
            return true;
    for (const QString& pat : m_title)
        if (!pat.isEmpty() && title.contains(pat, Qt::CaseInsensitive))
            return true;
    return false;
}

QStringList AdBlocker::allowProcPatterns() const
{
    QStringList copy = m_allowProc;
    copy.removeDuplicates();
    return copy;
}

QStringList AdBlocker::allowTitlePatterns() const
{
    QStringList copy = m_allowTitle;
    copy.removeDuplicates();
    return copy;
}

bool AdBlocker::allowed(const QString& title, const QString& process) const
{
    for (const QString& pat : m_allowProc)
        if (!pat.isEmpty() && process.contains(pat, Qt::CaseInsensitive))
            return true;
    for (const QString& pat : m_allowTitle)
        if (!pat.isEmpty() && title.contains(pat, Qt::CaseInsensitive))
            return true;
    return false;
}

void AdBlocker::setAllowPatterns(const QStringList& proc, const QStringList& title)
{
    m_allowProc = proc;
    m_allowTitle = title;
    save();
}

void AdBlocker::addAllowProc(const QString& p)
{
    const QString v = p.toLower();
    if (v.isEmpty() || m_allowProc.contains(v))
        return;
    m_allowProc.append(v);
    save();
}

void AdBlocker::addAllowTitle(const QString& t)
{
    if (t.isEmpty() || m_allowTitle.contains(t))
        return;
    m_allowTitle.append(t);
    save();
}

QString AdBlocker::allowProcFor(const QString& imageName)
{
    return imageName.toLower().remove(QLatin1String(".exe"));
}

QString AdBlocker::coveredByAllowProc(const QStringList& patterns, const QString& imageName)
{
    const QString one = allowProcFor(imageName);
    if (one.isEmpty())
        return {};
    for (const QString& pat : patterns) {
        const QString p = pat.trimmed().toLower();
        if (p.isEmpty())
            continue;                       // 空片段不算盖住（`allowed()` 那侧也跳过它）
        if (one.contains(p, Qt::CaseInsensitive))
            return pat.trimmed();
    }
    return {};
}

AdBlocker::Verdict AdBlocker::verdictOf(const Hit& h) const
{
    if (allowed(h.title, h.process))
        return Verdict::Allowed;
    if (!matches(h.title, h.process))
        return Verdict::NoMatch;
    if (h.width > kBigWidth || h.height > kBigHeight)
        return Verdict::TooBig;
    return m_observe ? Verdict::WouldCloseIfNotObserving : Verdict::WillClose;
}

QString AdBlocker::verdictText(Verdict v)
{
    switch (v) {
    case Verdict::Allowed: return QStringLiteral("白名单放行");
    case Verdict::NoMatch: return QStringLiteral("不匹配");
    case Verdict::TooBig: return QStringLiteral("命中，但按大窗口放过");
    case Verdict::WouldCloseIfNotObserving: return QStringLiteral("会关闭（观察模式中，现在不动手）");
    case Verdict::WillClose: return QStringLiteral("会被关闭");
    }
    return QString();
}

QVector<AdBlocker::Hit> AdBlocker::scan() const
{
    QVector<Hit> all;
    EnumWindows(enumOne, reinterpret_cast<LPARAM>(&all));
    return all;
}

int AdBlocker::closeHits(const QVector<Hit>& hits)
{
    int closed = 0;
    for (const Hit& h : hits) {
        if (!h.hwnd || !IsWindow(h.hwnd))
            continue;
        // 只关"看起来是弹窗"的：标题里带"推广"的浏览器/IDE 主窗口不能被顺手关掉
        if (h.width > kBigWidth || h.height > kBigHeight)
            continue;
        PostMessageW(h.hwnd, WM_CLOSE, 0, 0);
        ++closed;
    }
    return closed;
}

void AdBlocker::record(const Hit& h, bool closed)
{
    const QString key = QString::number(h.pid) + QLatin1Char('|') + h.title;
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime prev = m_lastRecorded.value(key);
    if (prev.isValid() && prev.msecsTo(now) < kRecordDedupeMs)
        return;                       // WM_CLOSE 没生效时下一轮还会命中，别把它记成好几个弹窗
    m_lastRecorded.insert(key, now);
    m_history.append(Record{now, h.title, h.process, h.pid, closed});
    while (m_history.size() > kHistoryKeep)
        m_history.removeFirst();
    saveHistory();
}

void AdBlocker::tick()
{
    if (!m_auto)
        return;
    QVector<Hit> hits;
    for (const Hit& h : scan())
        if (matches(h.title, h.process))
            hits.append(h);
    if (hits.isEmpty())
        return;
    const int closed = m_observe ? 0 : closeHits(hits);
    for (const Hit& h : hits)
        record(h, !m_observe);
    Utils::log(QString("adblock: hit=%1 closed=%2 observe=%3")
                   .arg(hits.size()).arg(closed).arg(m_observe));
}

int AdBlocker::closedToday() const
{
    const QDate today = QDate::currentDate();
    int n = 0;
    for (const Record& r : m_history)
        if (r.when.date() == today && r.closed)
            ++n;
    return n;
}

// 最近 days 天每天关了多少个（含 0 的天也要占位，柱状图才对得齐）
QVector<QPair<QDate, int>> AdBlocker::dailyCounts(int days) const
{
    QVector<QPair<QDate, int>> out;
    const QDate today = QDate::currentDate();
    for (int i = days - 1; i >= 0; --i)
        out.append(qMakePair(today.addDays(-i), 0));
    for (const Record& r : m_history) {
        if (!r.closed)
            continue;
        const int idx = days - 1 - int(r.when.date().daysTo(today));
        if (idx >= 0 && idx < out.size())
            ++out[idx].second;
    }
    return out;
}

QVector<QPair<QString, int>> AdBlocker::topProcesses(int days, int limit) const
{
    const QDate since = QDate::currentDate().addDays(-(days - 1));
    QHash<QString, int> count;
    for (const Record& r : m_history)
        if (r.closed && r.when.date() >= since)
            ++count[r.process.toLower()];
    QVector<QPair<QString, int>> out;
    for (auto it = count.keyBegin(); it != count.keyEnd(); ++it)
        out.append(qMakePair(*it, count.value(*it)));
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    while (out.size() > limit)
        out.removeLast();
    return out;
}

void AdBlocker::setAutoEnabled(bool on)
{
    if (m_auto == on)
        return;
    m_auto = on;
    save();
    Utils::log(QString("adblock: auto=%1").arg(on));
}

void AdBlocker::setObserveOnly(bool on)
{
    if (m_observe == on)
        return;
    m_observe = on;
    save();
}

void AdBlocker::setPatterns(const QStringList& proc, const QStringList& title)
{
    m_proc = proc;
    m_title = title;
    save();
}

void AdBlocker::addProcPattern(const QString& p)
{
    const QString v = p.toLower();
    if (v.isEmpty() || m_proc.contains(v))
        return;
    m_proc.append(v);
    save();
}

void AdBlocker::clearHistory()
{
    m_history.clear();
    m_lastRecorded.clear();
    saveHistory();
}
