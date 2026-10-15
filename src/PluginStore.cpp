#include "PluginStore.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include "ltool-plugin.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QDir>
#include <QNetworkProxy>
#include <QSettings>
#include <QSet>
#include <QUrl>
#include <algorithm>
#include <memory>

#include "DevEnv.h"
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

// 商店里的大小一律说 MB：小于 1MB 的也写成 0.28 MB，别在卡片上留一串几位数的字节。
QString humanSize(qint64 bytes)
{
    if (bytes <= 0)
        return QStringLiteral("大小不明");
    return QStringLiteral("%1 MB").arg(double(bytes) / 1048576.0, 0, 'f', 2);
}

QString stateWord(PluginStore::State s)
{
    using S = PluginStore::State;
    switch (s) {
    case S::Missing: return QStringLiteral("能装");
    case S::Update: return QStringLiteral("可更新");
    case S::Installed: return QStringLiteral("已装");
    case S::Disabled: return QStringLiteral("已禁用");
    case S::ApiMismatch: return QStringLiteral("装不了");
    case S::LocalOnly: return QStringLiteral("清单外");
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

namespace {

// 每条候选试两次。"TCP 连上、TLS 完成、然后整 5 秒不吐字节"在这条线路上是常态不是意外。
constexpr int kTriesPerCandidate = 2;

struct Chain {
    QVector<Attempt> attempts;
    int idx = -1;                    // 当前这条候选（-1 = 还没开始）
    int left = 0;                    // 这条还剩几次没试
    int tries = 0;                   // 一共试了几次（回报用）
    Fetched last;                    // 最后一条失败的样子，全挂就拿它回话
    QNetworkAccessManager* nam = nullptr;
    QObject* parent = nullptr;
    std::function<void()> before;
    std::function<bool(const QByteArray&)> chunk;
    std::function<void(qint64, qint64)> progress;
    std::function<void(const Fetched&)> done;
};

using ChainPtr = std::shared_ptr<Chain>;

void step(const ChainPtr& chain);

// 同一条还有额度就原地重来，额度用完才往下一条（镜像 → 代理 → 直连）走
void advance(const ChainPtr& chain)
{
    if (chain->left > 0) {
        --chain->left;
        step(chain);
        return;
    }
    ++chain->idx;
    if (chain->idx >= chain->attempts.size()) {
        chain->done(chain->last);
        return;
    }
    chain->left = kTriesPerCandidate - 1;
    step(chain);
}

void step(const ChainPtr& chain)
{
    const Attempt a = chain->attempts[chain->idx];
    ++chain->tries;
    if (chain->before)
        chain->before();
    if (chain->nam) {
        chain->nam->deleteLater();
        chain->nam = nullptr;
    }
    QObject* parent = chain->parent;
    chain->nam = new QNetworkAccessManager(parent);
    if (a.useProxy)
        DevEnv::applyProxy(chain->nam);            // 留空 = 系统代理，和产品里其他地方同一条
    else
        chain->nam->setProxy(QNetworkProxy::NoProxy);
    QNetworkRequest req { QUrl(a.url) };
    req.setTransferTimeout(20000);
    QNetworkReply* reply = chain->nam->get(req);
    // 这两个标志活得和回调一样长，所以放堆上：捕获引用会悬空
    auto localFail = std::make_shared<bool>(false);
    auto gotAny = std::make_shared<bool>(false);
    if (chain->chunk) {
        QObject::connect(reply, &QNetworkReply::readyRead, parent, [reply, chain, localFail, gotAny] {
            const QByteArray part = reply->readAll();
            *gotAny = true;
            if (part.isEmpty() || chain->chunk(part))
                return;
            *localFail = true;
        });
    }
    if (chain->progress) {
        QObject::connect(reply, &QNetworkReply::downloadProgress, parent,
                         [chain](qint64 got, qint64 all) { chain->progress(got, all); });
    }
    QObject::connect(reply, &QNetworkReply::finished, parent, [chain, reply, a, localFail, gotAny] {
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString netErr = reply->error() != QNetworkReply::NoError ? reply->errorString() : QString();
        Fetched one;
        one.tries = chain->tries;
        if (!chain->chunk)
            one.body = reply->readAll();
        reply->deleteLater();
        chain->nam = nullptr;
        if (netErr.isEmpty() && !*localFail) {
            one.ok = true;
            one.via = a.label;
            chain->done(one);
            return;
        }
        one.err = QStringLiteral("%1（HTTP %2%3，走的是 %4）")
                      .arg(netErr.isEmpty() ? QStringLiteral("本地写不下去") : netErr)
                      .arg(http)
                      .arg(*gotAny ? QStringLiteral("，已经收到一段") : QString(), a.url);
        Utils::log(QStringLiteral("fetch: %1 这条没成：%2").arg(a.label, one.err));
        chain->last = one;
        advance(chain);
    });
}

} // namespace

QString mirrorList()
{
    QSettings cfg(Utils::dataDir() + "/config.ini", QSettings::IniFormat);
    const QString custom = cfg.value(QStringLiteral("store/mirrors")).toString().trimmed();
    if (!custom.isEmpty())
        return custom;
    // 这台机器上量过还活着、且和原件逐字节一致的两条（2026-09-28 拿同一文件比过四家 sha；
    // 2026-10-11 量过带宽：gh-proxy.com 8.3MB/s、ghfast.top 2.2MB/s）。
    // mirror.ghproxy.com 21 秒超时、raw.gitmirror.com / raw.kkgithub.com 已经解不到 DNS；
    // cdn.jsdelivr.net 缓存约 7 天 —— 拿它装插件会一直"哈希对不上"，所以不放进来。
    return QStringLiteral("https://gh-proxy.com/,https://ghfast.top/");
}

QVector<Attempt> attemptsFor(const QString& originUrl)
{
    QVector<Attempt> out;
    const QUrl u(originUrl);
    // 镜像只代理 github raw 那一路。用户把索引地址指到自己的服务器时还套镜像，
    // 拿回来的是 404，他会以为自己的源坏了 —— 那种情况只留代理和直连两条腿。
    if (u.host() == QLatin1String("raw.githubusercontent.com")) {
        const QStringList mirrors = mirrorList().split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString& raw : mirrors) {
            QString m = raw.trimmed();
            if (m.isEmpty())
                continue;
            if (!m.endsWith(QLatin1Char('/')))
                m += QLatin1Char('/');
            out.append({ m + originUrl, false, QStringLiteral("镜像 ") + QUrl(m).host() });
        }
    }
    out.append({ originUrl, true, QStringLiteral("本地代理") });
    out.append({ originUrl, false, QStringLiteral("直连") });
    return out;
}

void fetch(const QString& originUrl, QObject* parent,
           const std::function<void()>& beforeAttempt,
           const std::function<bool(const QByteArray&)>& onChunk,
           const std::function<void(qint64, qint64)>& onProgress,
           const std::function<void(const Fetched&)>& done)
{
    auto chain = std::make_shared<Chain>();
    chain->attempts = attemptsFor(originUrl);
    chain->parent = parent;
    chain->before = beforeAttempt;
    chain->chunk = onChunk;
    chain->progress = onProgress;
    chain->done = done;
    if (chain->attempts.isEmpty()) {
        Fetched f;
        f.err = QStringLiteral("没有可试的地址");
        done(f);
        return;
    }
    advance(chain);
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
        e.version = o.value(QStringLiteral("version")).toString().trimmed();
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
                *note = QStringLiteral("已装，哈希就是清单里那个 · %1")
                            .arg(humanSize(QFileInfo(live).size()));
            return State::Installed;
        }
        if (note)
            *note = QStringLiteral("本机 %1… ≠ 清单 %2…（%3）")
                        .arg(got.left(8), e.sha256.left(8))
                        .arg(humanSize(QFileInfo(live).size()));
        return State::Update;
    }
    if (QFileInfo::exists(live + QStringLiteral(".off"))) {
        if (note)
            *note = QStringLiteral("%1 被改名成 .off（%2），下次启动不会加载它")
                        .arg(e.file, humanSize(QFileInfo(live + QStringLiteral(".off")).size()));
        return State::Disabled;
    }
    if (note)
        *note = QStringLiteral("%1 没装（清单 %2）").arg(e.file).arg(humanSize(e.bytes));
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

QVector<Row> localRows(const QString& dir)
{
    const QString d = local(dir);
    QVector<Row> out;
    const QStringList files = QDir(d).entryList({ QStringLiteral("*.dll"), QStringLiteral("*.dll.off") },
                                                QDir::Files | QDir::Hidden);
    for (const QString& f : files) {
        const bool off = f.endsWith(QStringLiteral(".off"));
        const QString base = off ? f.left(f.size() - 4) : f;
        Row r;
        r.remote.file = base;
        QString id = base;
        if (id.startsWith(QStringLiteral("ltool-")))
            id = id.mid(6);
        id.chop(4);   // .dll
        r.remote.id = id;
        // 元数据只读 dll 里嵌的那份 plugin.json（PluginHost::scan 的产物），不加载 dll；
        // .off 那个不在扫描结果里（宿主根本不会去认它），就只能报文件名。
        if (const PluginHost::Meta* m = PluginHost::meta(id)) {
            r.remote.name = m->name;
            r.remote.desc = m->desc;
            r.remote.color = QStringLiteral("%1,%2,%3").arg(m->color.red())
                                                           .arg(m->color.green()).arg(m->color.blue());
            r.remote.keywords = m->keywords;
            r.remote.actions = m->actions;
            r.remote.api = m->api;
        } else {
            r.remote.name = base;
            r.remote.api = LTOOL_PLUGIN_API_VERSION;
        }
        r.remote.bytes = QFileInfo(d + QLatin1Char('/') + f).size();
        r.state = off ? State::Disabled : State::Installed;
        r.note = off ? QStringLiteral("%1 被改名成 .off（%2），下次启动不会加载它")
                        .arg(base, humanSize(r.remote.bytes))
                     : QStringLiteral("%1 · %2").arg(base, humanSize(r.remote.bytes));
        out.append(r);
    }
    return out;
}

QVector<Row> localOnly(const QVector<Entry>& listed, const QString& dir)
{
    // 判"在不在清单里"按**文件名**：清单给的就是文件名，而目录里那个文件名是唯一的事实。
    QSet<QString> known;
    for (const Entry& e : listed)
        known.insert(e.file.toLower());
    QVector<Row> out;
    for (const Row& r : localRows(dir)) {
        if (known.contains(r.remote.file.toLower()))
            continue;
        Row copy = r;
        copy.state = r.state == State::Disabled ? State::Disabled : State::LocalOnly;
        copy.note = r.state == State::Disabled
                        ? QStringLiteral("%1 被改名成 .off（%2），清单里也没有这一条")
                              .arg(r.remote.file, humanSize(r.remote.bytes))
                        : QStringLiteral("清单里没有这一条（本机 %1）—— 能禁用，谈不上更新")
                              .arg(humanSize(r.remote.bytes));
        out.append(copy);
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
    const QString dest = local(dir) + QLatin1Char('/') + e.file;
    // Windows 上 QFile::rename **不会覆盖已存在的目标**（Qt 直接返回失败），所以「更新」这一条
    // 必须先把旧的那颗拿掉。以前没有这一步：12 颗"可更新"全在 rename 上撞死，
    // 报的还是那句"被别的程序挡住（多半是杀毒）"—— 冤枉了杀软两个小时。
    if (QFile::exists(dest) && !QFile::remove(dest)) {
        QFile::remove(partPath);
        return QStringLiteral("旧的 %1 换不掉（这个进程还在用它）—— 下好的那份先删掉了，"
                              "重启 ltool 之后再点一次「更新」就行").arg(e.file);
    }
    QString err;
    if (!Utils::renameStaged(partPath, dest, &err))
        return err;
    return QString();
}

namespace {

// 待删表就是配置里一个字符串列表。它存在的原因是"删不动"这个状态必须能跨进程活着：
// 这一轮加载过的 dll 只有下次启动（还没加载它的时候）才真删得掉。
QStringList pendingList()
{
    QSettings cfg(Utils::dataDir() + "/config.ini", QSettings::IniFormat);
    return cfg.value(QStringLiteral("store/pendingRemove")).toStringList();
}

void setPendingList(const QStringList& v)
{
    QSettings cfg(Utils::dataDir() + "/config.ini", QSettings::IniFormat);
    cfg.setValue(QStringLiteral("store/pendingRemove"), v);
    cfg.sync();   // 后面新开的 QSettings 才读得到（产品里其他地方也是这个约定）
}

// 一个插件的两个文件名都清掉：活着的那个 + 被改名禁用的那个
bool removeBoth(const QString& dir, const QString& file, bool* busy)
{
    bool allGone = true;
    for (const QString& suffix : { QString(), QStringLiteral(".off") }) {
        const QString path = dir + QLatin1Char('/') + file + suffix;
        if (!QFileInfo::exists(path))
            continue;
        if (QFile::remove(path))
            continue;
        allGone = false;               // Windows：这个镜像正在被进程用着就是这个形状
        if (busy)
            *busy = true;
    }
    return allGone;
}

} // namespace

QString uninstall(const QString& file, const QString& dir)
{
    const QString d = local(dir);
    const QString live = d + QLatin1Char('/') + file;
    if (!QFileInfo::exists(live) && !QFileInfo::exists(live + QStringLiteral(".off"))) {
        QStringList p = pendingList();
        if (p.removeAll(file) > 0) {          // 之前排过队、文件已经不在了：把账清掉
            setPendingList(p);
            return QString();
        }
        return QStringLiteral("%1 没装过，没东西可删").arg(file);
    }
    bool busy = false;
    if (removeBoth(d, file, &busy)) {
        QStringList p = pendingList();
        if (p.removeAll(file) > 0)
            setPendingList(p);
        Utils::log(QStringLiteral("store: 卸载了 %1").arg(file));
        return QString();
    }
    if (!busy)
        return QStringLiteral("%1 删不掉，也没说是被占用 —— 看日志").arg(file);
    QStringList p = pendingList();
    if (!p.contains(file))
        p << file;
    setPendingList(p);
    Utils::log(QStringLiteral("store: %1 这个进程还加载着，删不掉 —— 已记进待删表，下次启动删").arg(file));
    return QStringLiteral("pending");
}

QVector<QString> pendingRemoves()
{
    QVector<QString> out;
    for (const QString& f : pendingList())
        out.append(f);
    return out;
}

int applyPendingRemoves(const QString& dir)
{
    const QStringList p = pendingList();
    if (p.isEmpty())
        return 0;
    const QString d = local(dir);
    QStringList still;
    int removed = 0;
    for (const QString& f : p) {
        bool busy = false;
        if (removeBoth(d, f, &busy))
            ++removed;
        else if (!busy)
            continue;               // 文件已经不在了，这条账自然销掉
        else
            still << f;             // 还是删不动（不太可能：这是启动最早的一段），留着下次
    }
    setPendingList(still);
    Utils::log(QStringLiteral("store: 待删表里真删掉了 %1 个插件，还剩 %2 个").arg(removed).arg(still.size()));
    return removed;
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
