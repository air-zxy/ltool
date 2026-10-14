#include "PluginStore.h"

#include "ltool-plugin.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QSettings>
#include <QUrl>
#include <algorithm>

#include "PluginHost.h"
#include "Utils.h"

namespace {

QString local(const QString& dir)
{
    // 商店所有本地判断都走这一个入口：留空就是宿主真正在扫的那个目录，
    // --storetest 传一个临时目录进来，就能拿假 dll 走完整条判断。
    return dir.isEmpty() ? PluginHost::pluginsDir() : dir;
}

} // namespace

namespace PluginStore {

QString stateWord(PluginStore::State s)
{
    using S = PluginStore::State;
    switch (s) {
    case S::Missing: return QStringLiteral("能装");
    case S::Update: return QStringLiteral("可更新");
    case S::Installed: return QStringLiteral("已装");
    case S::Disabled: return QStringLiteral("已禁用");
    case S::ApiMismatch: return QStringLiteral("装不了");
    case S::Blocked: return QStringLiteral("条目不能用");
    }
    return QStringLiteral("条目不能用");
}

QString defaultIndexUrl()
{
    // 那份 dist/ 是直接提交在插件仓库 main 上的，raw 地址免登录就能下 —— 商店不需要 api 配额。
    return QStringLiteral("https://raw.githubusercontent.com/mateo-ki/ltool_plugin/main/dist/index.json");
}

QString indexUrl()
{
    QSettings cfg(Utils::dataDir() + "/config.ini", QSettings::IniFormat);
    const QString custom = cfg.value(QStringLiteral("store/indexUrl")).toString().trimmed();
    return custom.isEmpty() ? defaultIndexUrl() : custom;
}

QVector<Entry> parseIndex(const QByteArray& json, QStringList* problems)
{
    QVector<Entry> out;
    QJsonParseError pe {};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (problems)
            *problems << QStringLiteral("清单不是能读的 JSON：%1").arg(pe.errorString());
        return out;
    }
    const QJsonObject root = doc.object();
    const int schema = root.value(QStringLiteral("schema")).toInt();
    if (schema != 1) {
        if (problems)
            *problems << QStringLiteral("清单的 schema 是 %1，这台机器上的 ltool 只认 1")
                             .arg(schema);
        // schema 不对就一条都不接：往下读只会把"装不上"变成一堆看不懂的失败
        return out;
    }
    const QJsonArray arr = root.value(QStringLiteral("plugins")).toArray();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        Entry e;
        e.id = o.value(QStringLiteral("id")).toString().trimmed();
        e.name = o.value(QStringLiteral("name")).toString();
        e.desc = o.value(QStringLiteral("desc")).toString();
        e.file = o.value(QStringLiteral("file")).toString().trimmed();
        e.url = o.value(QStringLiteral("url")).toString().trimmed();
        e.sha256 = o.value(QStringLiteral("sha256")).toString().trimmed();
        e.color = o.value(QStringLiteral("color")).toString();
        e.bytes = qint64(o.value(QStringLiteral("bytes")).toDouble());
        e.api = o.value(QStringLiteral("api")).toInt();
        for (const QJsonValue& k : o.value(QStringLiteral("keywords")).toArray())
            e.keywords << k.toString();
        for (const QJsonValue& a : o.value(QStringLiteral("actions")).toArray())
            e.actions << a.toString();

        // 文件名是清单给的，而我们要拿它拼出一条本地路径 —— 这三条不是防御性代码，
        // 是"一份被改过的清单能把文件写到目录外面"和"不能"的区别。
        QStringList why;
        if (e.id.isEmpty())
            why << QStringLiteral("没有 id");
        if (e.file.isEmpty() || e.file != QFileInfo(e.file).fileName())
            why << QStringLiteral("文件名不像一个纯文件名：%1").arg(e.file.isEmpty() ? QStringLiteral("(空)") : e.file);
        else if (!e.file.endsWith(QStringLiteral(".dll"), Qt::CaseInsensitive))
            why << QStringLiteral("%1 不是 .dll").arg(e.file);
        const auto isHex = [](QChar ch) {
            const char16_t x = ch.unicode();
            return (x >= u'0' && x <= u'9') || (x >= u'a' && x <= u'f') || (x >= u'A' && x <= u'F');
        };
        if (e.sha256.size() != 64 || !std::all_of(e.sha256.begin(), e.sha256.end(), isHex))
            why << QStringLiteral("sha256 不是一个 64 位十六进制串（长度 %1）").arg(e.sha256.size());
        const QUrl u(e.url);
        if (!u.isValid() || u.scheme() != QLatin1String("https"))
            why << QStringLiteral("%1 的下载地址不是 https：%2").arg(e.id, e.url);
        if (!why.isEmpty()) {
            const QString label = e.id.isEmpty() ? QStringLiteral("(无名条目)") : e.id;
            const QString line = QStringLiteral("%1：%2").arg(label, why.join(QStringLiteral("，")));
            if (problems)
                *problems << line;
            continue;
        }
        out.append(e);
    }
    if (out.isEmpty() && problems && problems->isEmpty())
        *problems << QStringLiteral("清单里没有一条插件（plugins 是空数组）");
    return out;
}

State compare(const Entry& e, QString* note, const QString& dir)
{
    const QString d = local(dir);
    if (e.api != LTOOL_PLUGIN_API_VERSION) {
        if (note)
            *note = QStringLiteral("清单里的 Api 是 %1，这台机器上的宿主只认 %2 —— "
                                   "要么这个插件太新（等 ltool 升级），要么太旧（作者要重编）")
                        .arg(e.api).arg(LTOOL_PLUGIN_API_VERSION);
        return State::ApiMismatch;
    }
    const QString live = d + QLatin1Char('/') + e.file;
    if (QFileInfo::exists(live)) {
        const QString got = sha256Of(live);
        if (got.compare(e.sha256, Qt::CaseInsensitive) == 0) {
            if (note)
                *note = QStringLiteral("已装，哈希就是清单里那个");
            return State::Installed;
        }
        if (note)
            *note = QStringLiteral("本机 %1… ≠ 清单 %2…（%3 B）")
                        .arg(got.left(8), e.sha256.left(8))
                        .arg(QFileInfo(live).size());
        return State::Update;
    }
    if (QFileInfo::exists(live + QStringLiteral(".off"))) {
        if (note)
            *note = QStringLiteral("%1 被改名成 .off，下次启动不会加载它").arg(e.file);
        return State::Disabled;
    }
    if (note)
        *note = QStringLiteral("%1 没装（清单 %2 B）").arg(e.file).arg(e.bytes);
    return State::Missing;
}

QVector<Row> rows(const QVector<Entry>& entries, const QString& dir)
{
    QVector<Row> out;
    out.reserve(entries.size());
    for (const Entry& e : entries) {
        Row r;
        r.remote = e;
        r.state = compare(e, &r.note, dir);
        out.append(r);
    }
    return out;
}

QString sha256Of(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f))
        return QString();
    return QString::fromLatin1(h.result().toHex());
}

QString finishDownload(const QString& partPath, const Entry& e, const QString& dir)
{
    const QString got = sha256Of(partPath);
    if (got.isEmpty()) {
        QFile probe(partPath);
        return QStringLiteral("%1 读不出来：%2").arg(QFileInfo(partPath).fileName(), probe.errorString());
    }
    if (got.compare(e.sha256, Qt::CaseInsensitive) != 0) {
        QFile::remove(partPath);
        return QStringLiteral("哈希对不上，这份已经删掉了（下到的 %1… ≠ 清单里的 %2…）"
                              "—— 多半是中间有人动了 dist/，别反复重试")
                    .arg(got.left(12), e.sha256.left(12));
    }
    if (e.bytes > 0 && QFileInfo(partPath).size() != e.bytes) {
        QFile::remove(partPath);
        return QStringLiteral("字节数对不上（下到 %1 ≠ 清单 %2），已经删掉了")
                    .arg(QFileInfo(partPath).size()).arg(e.bytes);
    }
    QString err;
    if (!Utils::renameStaged(partPath, local(dir) + QLatin1Char('/') + e.file, &err))
        return err;
    return QString();
}

QString setEnabled(const QString& file, bool on, const QString& dir)
{
    const QString d = local(dir);
    const QString live = d + QLatin1Char('/') + file;
    const QString off = live + QStringLiteral(".off");
    if (on) {
        if (QFileInfo::exists(live))
            return QString();       // 本来就是启用状态，不动
        if (!QFileInfo::exists(off))
            return QStringLiteral("%1 和它的 .off 都不在 %2，没什么可以恢复").arg(file, d);
        QString err;
        return Utils::renameStaged(off, live, &err) ? QString() : err;
    }
    if (!QFileInfo::exists(live))
        return QFileInfo::exists(off) ? QString()
                                      : QStringLiteral("%1 没装，用不着禁用").arg(file);
    QString err;
    return Utils::renameStaged(live, off, &err) ? QString() : err;
}

} // namespace PluginStore
