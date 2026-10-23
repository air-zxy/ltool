#include "PluginStore.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include "ltool-plugin.h"
#include "platform/PluginNames.h"

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
#include <QHash>
#include <QPointer>
#include <QUrl>
#include <algorithm>
#include <memory>

#include "DevEnv.h"
#include "PluginHost.h"
#include "Utils.h"
#include "AboutPage.h"   // 版本号唯一来源在那儿：商店地址要按"我这版 core"去配

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
    case S::Different: return QStringLiteral("与商店不同");
    case S::Blocked: return QStringLiteral("条目不能用");
    }
    return QStringLiteral("条目不能用");
}

bool countsAsInstalled(State s, bool queuedRemove)
{
    using S = PluginStore::State;
    // 盘上有这颗 dll 就算"已装"——包括被禁的（<file>.off 还在）和排进待删的（还没删成）。
    // Missing/ApiMismatch/Blocked 三档盘上没有：前者是"能装"，后两者是"这台机器装不了/条目坏了"。
    switch (s) {
    case S::Installed:
    case S::Update:
    case S::Different:
    case S::LocalOnly:
    case S::Disabled:
        return true;
    case S::Missing:
    case S::ApiMismatch:
    case S::Blocked:
        break;
    }
    return queuedRemove;   // 排进待删表而状态还是 Missing 的话（卸到一半重扫），文件在盘上就算在
}

QByteArray loadCachedIndex()
{
    QFile f(Utils::dataDir() + QStringLiteral("/plugins_cache.json"));
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    return f.readAll();
}

bool saveCachedIndex(const QByteArray& json)
{
    if (json.isEmpty())
        return false;
    QDir().mkpath(Utils::dataDir());
    QFile f(Utils::dataDir() + QStringLiteral("/plugins_cache.json"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(json) == json.size();
}

bool shouldAutoRefresh()
{
    QSettings cfg(Utils::dataDir() + QStringLiteral("/config.ini"), QSettings::IniFormat);
    const QString last = cfg.value(QStringLiteral("store/lastFetchDate")).toString().trimmed();
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    return last != today;
}

void markFetched()
{
    QSettings cfg(Utils::dataDir() + QStringLiteral("/config.ini"), QSettings::IniFormat);
    cfg.setValue(QStringLiteral("store/lastFetchDate"), QDate::currentDate().toString(Qt::ISODate));
    cfg.setValue(QStringLiteral("store/lastFetchTime"), QDateTime::currentDateTime().toString(Qt::ISODate));
}

QString defaultIndexUrl()
{
    // 商店读的是**我自己这一版 core 配套发出去的那份清单**：标签就是 "v" + ltool.iss 里那个
    // 版本号（CI 有一步专门验它对得上），发版那次构建同时产出 ltoolcore.dll 和这 19 颗 dll，
    // 所以清单里每一条 sha256 都是"和跑着的那个 core 同一次编出来"的。
    //
    // 为什么不是插件仓库 main 上的 dist/index.json（原来那条），也不用 releases/latest：
    // dist 那批 dll 是人手工把本机构建产物提交进 git 的，最后一次 10-16，而 core 之后一直没停 ——
    // 于是"发一次商店"和"发一次 core"物理上就是两件事，10-20 那次磁盘清理闪退就是这么来的
    // （老 dll 按老偏移读 DiskScan::Tree，名字全齐、LoadLibrary 也成功，一用就崩）。
    // latest 另外两个坑：带 -beta.N 的预发版它不回，而且老 core 会拿到新 dll。
    // 这一版没发过商店资产就是 404，界面照实说"清单拉不到"，不会退回那批可能对不上的产物。
    // 直链的式子住在 AboutPage（安装包那条和这里必须是同一个式子，仓库地址不写第二处）。
    return AboutPage::releaseAssetUrl(AboutPage::currentReleaseTag(),
                                      QStringLiteral("plugins-index.json"));
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
    const bool isRaw = u.host() == QLatin1String("raw.githubusercontent.com");
    // 发版资产那一路（github.com/<仓库>/releases/download/<标签>/<文件>）镜像站同样代理，
    // 而且这条线路 github.com 直连间歇挂死是常态，比 raw 更需要那两条腿。
    const bool isAsset = u.host() == QLatin1String("github.com")
                       && u.path().contains(QLatin1String("/releases/download/"));
    // 镜像只代理 github 那两路。用户把索引地址指到自己的服务器时还套镜像，
    // 拿回来的是 404，他会以为自己的源坏了 —— 那种情况只留代理和直连两条腿。
    if (isRaw || isAsset) {
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
        else if (!e.file.endsWith(QLatin1Char('.') + Platform::pluginFileSuffix(), Qt::CaseInsensitive))
            why << QStringLiteral("%1 不是这台机器的插件后缀（这里要 .%2）")
                       .arg(e.file, Platform::pluginFileSuffix());
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

// 本机那一颗的状态。**接口号对不上的那一条不再在这里判**（2026-10-20 他报"注册表/开机分析还是点不开"）：
// 原来第一行就 `return ApiMismatch`，于是**已经装着、也真能打开**的那张卡被锁成"装不了"，
// 一颗按钮都不给、点正文也没反应 —— 商店那条能不能装，和本机这颗能不能用，是两件事。
// 接口号那一半挪到下面的 compare()：没装着的时候才叫"装不了"，装着的时候只是"换不了商店那份"。
static State localCompare(const Entry& e, QString* note, const QString& dir)
{
    const QString d = local(dir);
    const QString live = d + QLatin1Char('/') + e.file;
    if (QFileInfo::exists(live)) {
        const QString got = sha256Of(live);
        if (got.compare(e.sha256, Qt::CaseInsensitive) == 0) {
            if (note)
                *note = QStringLiteral("已装，哈希就是清单里那个 · %1")
                            .arg(humanSize(QFileInfo(live).size()));
            return State::Installed;
        }
        // 哈希不同只证明"不是同一颗"，不证明"有新版本"。商店每跑一次 CI 就重编一遍，
        // MSVC 的产物哈希天生变 —— 拿哈希当"可更新"会让所有人每次发版都看到一片「可更新」，
        // 而版本号一动不动（2026-10-17 实测：清单 14 条 version 全是 1.0.0，14 张全报可更新）。
        const QString localVer = PluginHost::metaVersionOf(live);
        const QString wantVer = e.version.trimmed();
        if (localVer.isEmpty() || wantVer.isEmpty()) {
            if (note)
                *note = QStringLiteral("本机 %1… ≠ 清单 %2…，而且有一侧读不到版本号（本机=%3 清单=%4）"
                                        "—— 说不清有没有新版，只说内容不同")
                            .arg(got.left(8), e.sha256.left(8),
                                 localVer.isEmpty() ? QStringLiteral("无") : localVer,
                                 wantVer.isEmpty() ? QStringLiteral("无") : wantVer);
            return State::Different;
        }
        const int cmp = Utils::compareVersions(wantVer, localVer);
        if (cmp > 0) {
            if (note)
                *note = QStringLiteral("本机 v%1 → 商店 v%2 · %3")
                            .arg(localVer, wantVer, humanSize(QFileInfo(live).size()));
            return State::Update;
        }
        if (note)
            *note = cmp < 0
                ? QStringLiteral("本机 v%1 比商店 v%2 还新（商店那份更旧）—— 按它会降级，别按")
                      .arg(localVer, wantVer)
                : QStringLiteral("版本号都是 v%1，但本机 %2… ≠ 商店 %3…（内容不同，没有新版）")
                      .arg(localVer, got.left(8), e.sha256.left(8));
        return State::Different;
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

// 商店那一条 + 本机这一颗，合起来给界面用的那一档。
State compare(const Entry& e, QString* note, const QString& dir)
{
    QString localNote;
    const State st = localCompare(e, &localNote, dir);
    if (e.api != LTOOL_PLUGIN_API_VERSION) {
        const QString why = QStringLiteral("清单里的 Api 是 %1，这台机器上的宿主只认 %2")
                                .arg(e.api).arg(LTOOL_PLUGIN_API_VERSION);
        if (st == State::Missing) {
            // 本机没有能用的那颗：这一条确实装不了，老那句原样留着
            if (note)
                *note = why + QStringLiteral(" —— 要么这个插件太新（等 ltool 升级），要么太旧（作者要重编）");
            return State::ApiMismatch;
        }
        // 本机装着（甚至正在跑）：那扇页不该因为"商店那条对不上号"就被锁死 ——
        // 只关掉从商店换一份那几颗出路，界面上说清是哪一半不行。
        if (note)
            *note = why + QStringLiteral("：换不了商店那份 · 本机这颗照旧（%1）").arg(localNote);
        return st;
    }
    if (note)
        *note = localNote;
    return st;
}

// 「这一条能不能从商店拿下来装」——界面只有这一个入口，别再自己比 api（比第二处就会和这里说法不一）。
bool installable(const Entry& e)
{
    return e.api == LTOOL_PLUGIN_API_VERSION;
}

QVector<Row> rows(const QVector<Entry>& entries, const QString& dir)
{
    QVector<Row> out;
    out.reserve(entries.size());
    for (const Entry& e : entries) {
        Row r;
        r.remote = e;
        r.state = compare(e, &r.note, dir);
        r.entryUsable = installable(e);
        out.append(r);
    }
    return out;
}

namespace { QStringList pendingList(); }   // 卸载排队那张表，定义在下面（uninstall 那一段）

QVector<Row> localRows(const QString& dir)
{
    const QString d = local(dir);
    QVector<Row> out;
    const QStringList files = QDir(d).entryList(
        Platform::pluginFileGlobs()
            << (QStringLiteral("*.") + Platform::pluginFileSuffix() + QStringLiteral(".off")),
        QDir::Files | QDir::Hidden);
    for (const QString& f : files) {
        const bool off = f.endsWith(QStringLiteral(".off"));
        const QString base = off ? f.left(f.size() - 4) : f;
        Row r;
        r.remote.file = base;
        const QString id = Platform::pluginIdOf(base);
        r.remote.id = id;
        // 元数据只读产物里嵌的那份 plugin.json（PluginHost::scan 的产物），不加载它；
        // .off 那个不在扫描结果里（宿主根本不会去认它），就只能报文件名。
        if (const PluginHost::Meta* m = PluginHost::meta(id)) {
            r.remote.name = m->name;
            r.remote.desc = m->desc;
            r.remote.color = QStringLiteral("%1,%2,%3").arg(m->color.red())
                                                           .arg(m->color.green()).arg(m->color.blue());
            r.remote.keywords = m->keywords;
            r.remote.actions = m->actions;
            r.remote.api = m->api;
            r.remote.version = m->version;
        } else {
            r.remote.name = base;
            r.remote.api = LTOOL_PLUGIN_API_VERSION;
        }
        r.remote.bytes = QFileInfo(d + QLatin1Char('/') + f).size();
        r.state = off ? State::Disabled : State::Installed;
        r.note = off ? QStringLiteral("%1 被改名成 .off（%2），下次启动不会加载它")
                        .arg(base, humanSize(r.remote.bytes))
                     : QStringLiteral("%1 · %2").arg(base, humanSize(r.remote.bytes));
        // 排过卸载队的那颗要说实话：文件还在、进程还加载着它，所以它现在既没"已卸载"也
        // 不是"还能再卸一次"。这句原先只写进卡片那颗标签（applyUninstall 的 state 参数），
        // 而点击之后整格会重建，那句就跟着旧标签一起没了 —— 界面上看起来就是"点了没反应"。
        if (pendingList().contains(base))
            r.note = QStringLiteral("%1 已安排卸载 · 这个进程还加载着它，下次启动真删掉（%2）")
                         .arg(base, humanSize(r.remote.bytes));
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

namespace {

QStringList pendingList()
{
    QSettings cfg(Utils::dataDir() + "/config.ini", QSettings::IniFormat);
    return cfg.value(QStringLiteral("store/pendingRemove")).toStringList();
}

void setPendingList(const QStringList& v)
{
    QSettings cfg(Utils::dataDir() + "/config.ini", QSettings::IniFormat);
    cfg.setValue(QStringLiteral("store/pendingRemove"), v);
    cfg.sync();
}

} // namespace

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
    // Windows 上正在被当前进程使用的 DLL 无法删除，但允许在同目录下重命名。
    // 如果直接删除旧 DLL 失败，说明当前进程正在占用它，此时重命名为 .old 并记录待删表，
    // 新下载的 DLL 就能顺利覆盖到位！
    if (QFile::exists(dest) && !QFile::remove(dest)) {
        const QString oldFile = dest + QStringLiteral(".old.%1").arg(QDateTime::currentMSecsSinceEpoch());
        QFile::remove(oldFile);
        if (QFile::rename(dest, oldFile)) {
            QStringList p = pendingList();
            const QString oldName = QFileInfo(oldFile).fileName();
            if (!p.contains(oldName)) {
                p << oldName;
                setPendingList(p);
            }
        } else {
            QFile::remove(partPath);
            return QStringLiteral("旧的 %1 换不掉（可能被其它程序锁定）：%2")
                        .arg(e.file, QFileInfo(dest).fileName());
        }
    }
    QString err;
    if (!Utils::renameStaged(partPath, dest, &err))
        return err;
    return QString();
}

namespace {

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

// ---- 装一颗（从商店页里搬出来的那半条下载路）----
namespace {

// 值 = 这一趟的号。放标记的时候必须对号：不然"上一趟的收尾"能把"下一趟刚挂上的标记"错放掉，
// 那道闸就白装了（两趟照样能同时写同一个 .part）。
QHash<QString, quint64>& inFlight()
{
    static QHash<QString, quint64> m;
    return m;
}

void releaseFlight(const QString& file, quint64 token)
{
    auto it = inFlight().find(file);
    if (it != inFlight().end() && it.value() == token)
        inFlight().erase(it);
}

} // namespace

bool installInFlight(const QString& file)
{
    return inFlight().contains(file);
}

void installEntry(const Entry& e, QObject* parent,
                  const std::function<void(qint64, qint64)>& onProgress,
                  const std::function<void(const InstallResult&)>& done)
{
    // 两趟写同一个 .part 必坏（而且第二趟的哈希一定对不上，看起来像"仓库被人动过"）。
    // 手动那路和自动那路确实可能同时点中同一颗，所以这道闸在这儿，不在调用方。
    if (inFlight().contains(e.file)) {
        done({false, QStringLiteral("这一颗已经有一趟在飞了，这趟不重复起"), QString()});
        return;
    }
    const QString dir = PluginHost::pluginsDir();
    QDir().mkpath(dir);
    const QString part = dir + QLatin1Char('/') + e.file + QStringLiteral(".part");
    static quint64 tokenSeed = 0;
    const quint64 token = ++tokenSeed;
    inFlight().insert(e.file, token);
    // 标记的生命周期 = **这趟下载的生命周期**，不是"回调有没有跑"：`fetch` 的连接全挂在 parent 上，
    // 商店页被拆掉就是 done 永远不回。以前那个计数是页面成员（跟着页一起没，不出事），搬进 core
    // 之后它是进程级的 —— 不跟着 parent 一起放掉，这颗 dll 就从此永远卡在「已经有一趟在飞」，
    // 而他每次点都是同一句话（2026-10-21 --storetest 那条红的就是这个）。
    QObject* flight = new QObject(parent);
    QObject::connect(flight, &QObject::destroyed,
                     [file = e.file, token](QObject*) { releaseFlight(file, token); });

    auto out = std::make_shared<QFile>(part);
    auto writeFailed = std::make_shared<bool>(false);
    // 每一颗只有一个收口：关掉 .part、销掉飞行标记、回话。少销一次 = 这颗以后永远"在飞"。
    QPointer<QObject> flightRef = flight;
    auto settle = [out, part, e, done, flightRef, token](const InstallResult& r) {
        if (out->isOpen())
            out->close();
        releaseFlight(e.file, token);
        if (flightRef)
            flightRef->deleteLater();
        done(r);
    };

    fetch(e.url, parent,
          [out, writeFailed] {
              // 每次尝试之前重开一遍 .part：上一条可能只写下了一半，续着写必坏
              if (out->isOpen())
                  out->close();
              *writeFailed = false;
              if (!out->open(QIODevice::WriteOnly | QIODevice::Truncate))
                  *writeFailed = true;
          },
          [out, writeFailed](const QByteArray& chunk) {
              if (*writeFailed)
                  return false;
              if (out->write(chunk) != chunk.size()) {
                  *writeFailed = true;
                  return false;
              }
              return true;
          },
          onProgress,
          [settle, out, part, writeFailed, e](const Fetched& got) {
              if (out->isOpen())
                  out->close();          // finishDownload 要读这个文件，句柄必须先放掉
              if (!got.ok || *writeFailed) {
                  QFile::remove(part);
                  settle({false,
                          got.err.isEmpty() ? QStringLiteral("没下完：本地写不进 .part")
                                            : QStringLiteral("没下完：%1").arg(got.err),
                          QString()});
                  return;
              }
              const QString err = finishDownload(part, e);
              if (!err.isEmpty()) {
                  settle({false, err, got.via});
                  return;
              }
              settle({true, QString(), got.via});
          });
}

// ---- 每天一次的自动更新（2026-10-21 他点名：「一天自动更新一次，如果有新插件」）----
namespace {

QSettings& autoCfg()
{
    static QSettings cfg(Utils::dataDir() + QStringLiteral("/config.ini"), QSettings::IniFormat);
    return cfg;
}

// 这一趟自己的状态：一次只跑一趟（启动那一下排的）。**不**和商店页那批共用队列 ——
// 那一批挂在 ToolsPage 上，页拆了队列就该跟着没；这一趟没有页。
QVector<Entry> g_autoQueue;
QStringList g_autoOk;
QStringList g_autoBad;
QString g_autoSrc;
QString g_autoWhy;            // 那句短话背后的全文（进日志 + 悬停说明，不进页面那一行）
std::function<void(const QString&)> g_autoFinish;

void writeAutoNote(const QString& note, const QString& why)
{
    autoCfg().setValue(QStringLiteral("store/autoUpdateNote"), note);
    autoCfg().setValue(QStringLiteral("store/autoUpdateWhy"), why);
    autoCfg().setValue(QStringLiteral("store/autoUpdateAt"),
                       QDateTime::currentDateTime().toString(Qt::ISODate));
}

void autoStep(QObject* owner)
{
    if (g_autoQueue.isEmpty()) {
        const QString note = autoUpdateSummary(g_autoOk, g_autoBad, g_autoSrc);
        writeAutoNote(note, g_autoWhy);
        // 日志是"全文"那一份的落点：页面上那句是短的，查的时候得在这儿看到 URL / 原始错误。
        Utils::log(QStringLiteral("autoupdate: %1 · %2").arg(note, g_autoWhy));
        if (g_autoFinish)
            g_autoFinish(note);
        g_autoFinish = nullptr;
        return;
    }
    const Entry e = g_autoQueue.takeFirst();
    installEntry(e, owner, {}, [owner, e](const InstallResult& r) {
        const QString who = e.name.isEmpty() ? e.file : e.name;
        if (r.ok)
            g_autoOk << who;
        else
            g_autoBad << QStringLiteral("%1 %2").arg(who, r.err);
        autoStep(owner);
    });
}

} // namespace

bool autoUpdateEnabled()
{
    // 默认**开**：他这句是"自动做 X"，不是"给我个按钮我按了才做"。
    return autoCfg().value(QStringLiteral("store/autoUpdate"), true).toBool();
}

bool autoUpdateDue()
{
    return autoCfg().value(QStringLiteral("store/autoUpdateDate")).toString().trimmed()
        != QDate::currentDate().toString(Qt::ISODate);
}

QVector<Entry> autoUpdateTargets(const QVector<Row>& all)
{
    // 只认 State::Update —— 已装的、清单版本比本机新的那一档。
    // Missing（清单里有、本机没装）**故意不在这里**：那是"新插件"，他拍的是仍要自己点。
    // Different（哈希不同但版本不比本机新）也不在：那多半是本机手工同步的更新的一份，
    // 自动拿商店那份盖上去就是把他的东西换掉。
    QVector<Entry> out;
    for (const Row& r : all)
        if (r.state == State::Update && r.entryUsable)
            out.append(r.remote);
    return out;
}

QString autoUpdateSummary(const QStringList& ok, const QStringList& bad, const QString& src)
{
    QString s;
    if (!ok.isEmpty())
        s = QStringLiteral("自动更新了 %1 颗：%2").arg(ok.size()).arg(ok.join(QStringLiteral("、")));
    else if (bad.isEmpty())
        s = QStringLiteral("没有要换的");
    else
        s = QStringLiteral("一颗都没换成");
    if (!bad.isEmpty())
        s += QStringLiteral("%1%2 颗没成：%3").arg(ok.isEmpty() ? QString() : QStringLiteral(" · "))
                 .arg(bad.size()).arg(bad.join(QStringLiteral("；")));
    // 「已落盘，下次启动用新的」这句**只在真换了东西的时候说**：一颗都没动就说"已落盘"，
    // 是拿一句像模像样的话盖住"其实什么都没发生"（第一版三种说法都带着它，判据里那条
    // "没动过文件的那句不许说已落盘"就是为这个加的）。
    if (!ok.isEmpty())
        s += QStringLiteral("（%1 · 已落盘，下次启动用新的）").arg(src);
    else
        s += QStringLiteral("（%1）").arg(src);
    return s;
}

QString autoBrief(const QString& stored)
{
    // 1.5.0 之前，note 里存的是**整条原文**（带 URL 和 HTTP 码那种）。他这次说的是别把它摆在页面上，
    // 所以读的时候也要过这一刀 —— 不然那行要一直难看到下一次自动更新跑完（最快也是明天）。
    // 切法：整条里只要出现过 http，就把第一个「（」之后的都当成"那是原文，不是给人看的那半句"。
    // 剩下的还是带 http（老格式里那种没有括号的失败句）⇒ 宁可退成一句固定的短话，也不把原文推上页面。
    if (!stored.contains(QStringLiteral("http"), Qt::CaseInsensitive))
        return stored;
    const int k = stored.indexOf(QStringLiteral("（"));
    if (k > 0)
        return stored.left(k).trimmed();
    return QStringLiteral("上次那一趟没成（原文在悬停说明里）");
}

QString autoUpdateNote()
{
    const QString what = autoBrief(autoCfg().value(QStringLiteral("store/autoUpdateNote")).toString());
    if (what.isEmpty())
        return QString();
    const QDateTime when = QDateTime::fromString(
        autoCfg().value(QStringLiteral("store/autoUpdateAt")).toString(), Qt::ISODate);
    return when.isValid() ? QStringLiteral("%1 · %2").arg(when.toString(QStringLiteral("MM-dd hh:mm")), what)
                          : what;
}

QString autoUpdateWhy()
{
    const QString why = autoCfg().value(QStringLiteral("store/autoUpdateWhy")).toString();
    // 没有单独的 why 就是老格式：那时全文就存在 note 里，悬停说明把它接回来，一个字都不丢。
    return why.isEmpty() ? autoCfg().value(QStringLiteral("store/autoUpdateNote")).toString() : why;
}

void runDailyUpdate(QObject* parent, const std::function<void(const QString&)>& onDone)
{
    static QObject owner;                 // 这趟没有窗口可挂：进程级，和 app 同寿
    QObject* const host = parent ? parent : &owner;
    // 回话里的空串 = **这一趟根本没跑**（没开，或今天已经跑过）。界面那行读的是配置里那句
    // note（autoUpdateNote），不是这个回调 —— 所以这里不该把上次的 note 再报一遍，
    // 否则每次启动都会多一条"自动更新花了 0 ms"的假心跳。
    if (!autoUpdateEnabled() || !autoUpdateDue()) {
        onDone(QString());
        return;
    }
    // 先记今天再动手：跑挂了也不至于每次启动都刷一遍网络（那的表现是"开机就卡"）。
    autoCfg().setValue(QStringLiteral("store/autoUpdateDate"),
                       QDate::currentDate().toString(Qt::ISODate));

    fetch(indexUrl(), host, {}, {}, {},
          [host, onDone](const Fetched& got) {
              QStringList problems;
              QVector<Entry> entries;
              QString src;
              QString why;
              if (got.ok) {
                  saveCachedIndex(got.body);
                  entries = parseIndex(got.body, &problems);
                  src = QStringLiteral("清单 %1 条").arg(entries.size());
                  why = QStringLiteral("清单 %1 条，走的是 %2，一共试了 %3 次")
                            .arg(entries.size()).arg(got.via).arg(got.tries);
              } else if (const QByteArray cached = loadCachedIndex(); !cached.isEmpty()) {
                  entries = parseIndex(cached, &problems);
                  // 页面上那句只说"网络没通、用的是缓存"；哪条链、什么错、几秒超时都进 why（日志 + 悬停）。
                  src = QStringLiteral("网络没通，用的本地缓存");
                  why = QStringLiteral("网络没成（%1），拿本地缓存那 %2 条清单试").arg(got.err)
                            .arg(entries.size());
              } else {
                  const QString note = QStringLiteral("今天没跑成，本地也没有缓存清单");
                  const QString why2 = QStringLiteral("今天没跑成：%1，本地也没有缓存清单").arg(got.err);
                  writeAutoNote(note, why2);
                  Utils::log(QStringLiteral("autoupdate: %1 · %2").arg(note, why2));
                  onDone(note);
                  return;
              }
              g_autoQueue = autoUpdateTargets(rows(entries));
              g_autoOk.clear();
              g_autoBad.clear();
              g_autoSrc = src;
              g_autoWhy = why;
              g_autoFinish = onDone;
              Utils::log(QStringLiteral("autoupdate: 开始（%1 条清单里挑出 %2 颗要换）")
                             .arg(entries.size()).arg(g_autoQueue.size()));
              autoStep(host);
          });
}

} // namespace PluginStore
