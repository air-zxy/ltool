#include "WebShortcuts.h"

#include "Utils.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>

namespace {

QString websFile() { return Utils::dataDir() + "/webs.json"; }

QVector<WebEntry> defaults()
{
    QVector<WebEntry> v;
    WebEntry ld;
    ld.keys = { "ld", "linuxdo" };
    ld.name = "LINUX DO";
    ld.url = "https://linux.do";
    v.append(ld);
    WebEntry gh;
    gh.keys = { "gh", "github" };
    gh.name = "GitHub";
    gh.url = "https://github.com";
    gh.searchUrl = "https://github.com/search?q=%s";
    v.append(gh);
    return v;
}

// webs.json 的那份 JSON 数组：saveWebs 和"默认那份的字节"都从这儿出，
// 免得「什么算默认值」有第二份答案（搬家那条判据就靠这两个字节相等来判断能否让路）。
QJsonArray websJsonArray(const QVector<WebEntry>& webs)
{
    QJsonArray arr;
    for (const WebEntry& e : webs) {
        QJsonObject o;
        QJsonArray keys;
        for (const QString& k : e.keys)
            keys.append(k.toLower());
        o.insert("keys", keys);
        o.insert("name", e.name);
        o.insert("url", e.url);
        if (!e.searchUrl.isEmpty())
            o.insert("searchUrl", e.searchUrl);
        arr.append(o);
    }
    return arr;
}

} // namespace

namespace WebShortcuts {

QByteArray stockWebsJson()
{
    return QJsonDocument(websJsonArray(defaults())).toJson(QJsonDocument::Indented);
}

void ensureDefaultWebs()
{
    // 还没接住你真数据的目录里别播默认表：搬家那条"目标已有同名不覆盖"会把它当真文件对待，
    // 于是你老目录里那份 24 条永远进不来（2026-10-21 本机实测丢的就是这个，见 Utils.h 那颗守卫）。
    if (Utils::legacyMigrationPending())
        return;
    if (QFile::exists(websFile()))
        return;
    saveWebs(defaults());
}

QVector<WebEntry> loadWebs()
{
    QVector<WebEntry> out;
    QFile f(websFile());
    if (!f.open(QIODevice::ReadOnly))
        return out;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isArray())
        return out;
    for (const QJsonValue& v : doc.array()) {
        const QJsonObject o = v.toObject();
        WebEntry e;
        for (const QJsonValue& k : o.value("keys").toArray())
            e.keys << k.toString().toLower();   // 比对用的是小写首词，存的时候就先归好
        e.name = o.value("name").toString();
        e.url = o.value("url").toString();
        e.searchUrl = o.value("searchUrl").toString();
        if (e.name.isEmpty() || e.url.isEmpty())
            continue;
        out.append(e);
    }
    return out;
}

bool saveWebs(const QVector<WebEntry>& webs)
{
    QSaveFile f(websFile());
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(websJsonArray(webs)).toJson(QJsonDocument::Indented));
    return f.commit();
}

bool looksLikeUrl(const QString& text)
{
    const QString t = text.trimmed();
    if (t.isEmpty() || t.contains(QLatin1Char(' ')) || t.contains(QLatin1Char('\t')))
        return false;
    if (t.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
        || t.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)
        || t.startsWith(QLatin1String("ftp://"), Qt::CaseInsensitive))
        return true;
    if (t.startsWith(QLatin1String("ms-settings:")))   // 那是系统页，走应用那条，不算网址
        return false;
    // 裸域名：必须有个点、点后至少两个字母、且冒号前不是单个盘符字母（C:\ 那种是路径）
    const int colon = t.indexOf(QLatin1Char(':'));
    if (colon == 1)
        return false;
    const QString host = t.section(QLatin1Char('/'), 0, 0);
    const int dot = host.lastIndexOf(QLatin1Char('.'));
    if (dot < 0 || dot + 1 >= host.size())
        return false;
    const QString tail = host.mid(dot + 1);
    if (tail.size() < 2)
        return false;
    for (const QChar c : tail)
        if (!(c == QLatin1Char('-') || c.isLetterOrNumber()))
            return false;
    return host.front() != QLatin1Char('.') && host.back() != QLatin1Char('.');
}

QString normalizeUrl(const QString& text)
{
    QString t = text.trimmed();
    if (t.isEmpty())
        return t;
    if (!t.contains(QLatin1String("://")))
        t.prepend("https://");
    return t;
}

QString engineSearchUrl(const QString& query)
{
    const QString q = QString::fromUtf8(QUrl::toPercentEncoding(query));
    const QString engine = QSettings(Utils::dataDir() + "/config.ini", QSettings::IniFormat)
                               .value("web/engine", "baidu").toString();
    if (engine == QLatin1String("google"))
        return "https://www.google.com/search?q=" + q;
    if (engine == QLatin1String("bing"))
        return "https://www.bing.com/search?q=" + q;
    if (engine == QLatin1String("duckduckgo"))
        return "https://duckduckgo.com/?q=" + q;
    return "https://www.baidu.com/s?wd=" + q;
}

} // namespace WebShortcuts
