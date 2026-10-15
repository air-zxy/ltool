// linux.do 盯帖插件自带的判据。**一条都不碰网络、一条都不弹气泡** ——
// 取回那条真路走 --linuxdonet（要联网，不进 CI），这里全是纯函数 + 离屏摆控件：
// 解析、命中、去重、前缀拼法、配置夹紧与回读、要不要弹那一句的决定、页面控件在不在布局里。
#include "LinuxdoPage.h"
#include "LinuxdoWatch.h"
#include "Utils.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QEventLoop>
#include <QSettings>
#include <QSpinBox>

#include <cstdio>
#include <functional>

namespace {

QByteArray fixture()
{
    // 三条真形状：id 乱序（12 / 10 / 8）、其中一条只有标签命中、还有一条没标题（该被丢掉）
    QJsonArray topics;
    const auto mk = [](double id, const QString& title, const QStringList& tags) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), id);
        o.insert(QStringLiteral("title"), title);
        o.insert(QStringLiteral("category_id"), 36);
        o.insert(QStringLiteral("created_at"), QStringLiteral("2026-10-15T14:49:33.923Z"));
        QJsonArray arr;
        for (const QString& s : tags)
            arr.append(s);
        o.insert(QStringLiteral("tags"), arr);
        return o;
    };
    topics.append(mk(12, QStringLiteral("【福利】新一轮抽奖开始"), {}));
    topics.append(mk(10, QStringLiteral("求推荐裙子"), { QStringLiteral("女装"), QStringLiteral("穿搭") }));
    topics.append(mk(8, QStringLiteral("公益站又挂了"), {}));
    topics.append(mk(99, QString(), {}));
    QJsonObject list;
    list.insert(QStringLiteral("topics"), topics);
    QJsonObject root;
    root.insert(QStringLiteral("topic_list"), list);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

} // namespace

extern "C" Q_DECL_EXPORT int ltoolPluginSelftest()
{
    int bad = 0;
    const auto check = [&bad](const char* name, const QString& got, const QString& want) {
        const bool ok = got == want;
        if (!ok)
            ++bad;
        std::printf("%s %s got=[%s] want=[%s]\n", ok ? "PASS" : "FAIL", name,
                    got.toUtf8().constData(), want.toUtf8().constData());
        std::fflush(stdout);
    };

    // ---- 解析 ----
    const QVector<LinuxdoTopic> topics = LinuxdoWatch_::parseTopics(fixture());
    check("解出三条（没标题那条被丢掉）", QString::number(topics.size()), "3");
    check("id 原样留着（乱序不重排）", QStringLiteral("%1-%2-%3").arg(topics[0].id).arg(topics[1].id)
              .arg(topics[2].id), "12-10-8");
    check("URL 是按 id 算出来的绝对地址", topics[0].url, QStringLiteral("https://linux.do/t/12"));
    check("标签读进来了", QString::number(topics[1].tags.size()), "2");
    check("Cloudflare 那张挑战页解不出任何一条",
          QString::number(LinuxdoWatch_::parseTopics("<html><title>Just a moment</title></html>")
                              .size()), "0");
    check("空字节不崩", QString::number(LinuxdoWatch_::parseTopics(QByteArray()).size()), "0");

    // ---- 命中 ----
    const QStringList words { QStringLiteral("福利"), QStringLiteral("女装"), QStringLiteral("公益") };
    const QVector<LinuxdoHit> all = LinuxdoWatch_::matchNew(topics, words, 0);
    check("lastSeen=0 时三条都算新命中", QString::number(all.size()), "3");
    check("标题命中的记下标题那个词", all[0].matched, QStringLiteral("福利"));
    check("只有标签命中的也算（记下标签撞的那个词）", all[1].matched, QStringLiteral("女装"));
    const QVector<LinuxdoHit> newer = LinuxdoWatch_::matchNew(topics, words, 10);
    check("上一轮报过的不再报第二遍（lastSeen=10 只剩 id 12）",
          QStringLiteral("%1/%2").arg(newer.size()).arg(newer[0].topic.id), "1/12");
    {
        QVector<LinuxdoTopic> two;
        LinuxdoTopic a; a.id = 20; a.title = QStringLiteral("福利 + 公益 两个词都撞");
        two.append(a);
        check("一条帖子只算一次（撞两个词也只冒一条）",
              QString::number(LinuxdoWatch_::matchNew(two, words, 10).size()), "1");
        LinuxdoTopic b; b.id = 21; b.title = QStringLiteral("今天天气不错");
        b.tags << QStringLiteral("天气");
        two.append(b);
        check("标题和标签都不撞就不算",
              QString::number(LinuxdoWatch_::matchNew(two, words, 20).size()), "0");
        check("空词不把所有帖子都算命中",
              QString::number(LinuxdoWatch_::matchNew(two, { QString(), QStringLiteral("  ") }, 0).size()),
              "0");
    }

    // ---- 前缀拼法 ----
    check("前缀带斜杠：拼出来不多不少一个斜杠",
          LinuxdoWatch_::relayUrl(QStringLiteral("https://r.jina.ai/"),
                                  QStringLiteral("https://linux.do/latest.json")),
          QStringLiteral("https://r.jina.ai/https://linux.do/latest.json"));
    check("前缀不带斜杠：一样", LinuxdoWatch_::relayUrl(QStringLiteral("https://a.b"),
                                                       QStringLiteral("https://c.d/e")),
          QStringLiteral("https://a.b/https://c.d/e"));
    check("前缀留空 = 只用直连（原样回）",
          LinuxdoWatch_::relayUrl(QString(), QStringLiteral("https://c.d/e")),
          QStringLiteral("https://c.d/e"));
    check("前缀两边有空格、末尾多斜杠都吃得下（只留一个分隔的斜杠）",
          LinuxdoWatch_::relayUrl(QStringLiteral("  https://a.b//  "), QStringLiteral("https://c.d")),
          QStringLiteral("https://a.b/https://c.d"));

    // ---- 要不要弹那一句 ----
    check("第一次跑不弹（只记下从哪条开始）", LinuxdoWatch_::shouldNotify(true, 9) ? "yes" : "no", "no");
    check("没命中不弹", LinuxdoWatch_::shouldNotify(false, 0) ? "yes" : "no", "no");
    check("命中了才弹", LinuxdoWatch_::shouldNotify(false, 2) ? "yes" : "no", "yes");

    // ---- 配置：写进去要能读回来，越界的要夹住（这一路跑在 --linuxdotest 的沙箱 APPDATA 里）----
    LinuxdoWatch& w = LinuxdoWatch::instance();
    check("默认是开着的（用户要的就是它自己会跑）", w.enabled() ? "yes" : "no", "yes");
    check("默认间隔 10 分钟", QString::number(w.intervalMinutes()), "10");
    check("默认三个词", w.keywords().join(QLatin1Char(',')), QStringLiteral("福利,女装,公益"));
    check("默认中转前缀就是实测能用那一条", w.prefix(), QStringLiteral("https://r.jina.ai/"));
    w.setIntervalMinutes(0);
    check("间隔下限卡住（0 分钟 = 3 分钟，别拿公开站点当 DDoS）", QString::number(w.intervalMinutes()), "3");
    w.setIntervalMinutes(9999);
    check("间隔上限卡住", QString::number(w.intervalMinutes()), "240");
    w.setKeywords({ QStringLiteral(" 福利 "), QStringLiteral("福利"), QStringLiteral(""),
                    QStringLiteral("抽奖") });
    check("关键词去空、去重、trim", w.keywords().join(QLatin1Char(',')), QStringLiteral("福利,抽奖"));
    w.setPrefix(QStringLiteral("  https://mine.example/ "));
    w.setKeywords({ QStringLiteral("女装") });
    w.setIntervalMinutes(17);
    {
        QSettings st(Utils::dataDir() + "/config.ini", QSettings::IniFormat);
        check("三样都真的落了盘", QStringLiteral("%1/%2/%3")
                  .arg(st.value(QStringLiteral("linuxdo/minutes")).toInt())
                  .arg(st.value(QStringLiteral("linuxdo/keywords")).toString(),
                       st.value(QStringLiteral("linuxdo/prefix")).toString())
                  .arg(st.value(QStringLiteral("linuxdo/enabled")).toBool() ? 1 : 0),
              QStringLiteral("17/女装/https://mine.example/"));
    }
    {
        // 再建一个引擎实例读一遍：回填和写入必须同源（页面每次打开都是新的）
        LinuxdoPage page;
        auto* spin = page.findChild<QSpinBox*>(QStringLiteral("linuxdoMinutes"));
        auto* words2 = page.findChild<QLineEdit*>(QStringLiteral("linuxdoWords"));
        auto* pre = page.findChild<QLineEdit*>(QStringLiteral("linuxdoPrefix"));
        auto* on = page.findChild<QCheckBox*>(QStringLiteral("linuxdoEnabled"));
        // 分隔符用 " | " 不用 "/"：前缀自己就以斜杠结尾，拿 "/" 拼会把期望值写成
        // "…mine.example/1"，而实际是 "…mine.example//1" —— 红了才发现测的是格式不是回填。
        check("页面回填的就是盘上那份", QStringLiteral("%1 | %2 | %3 | %4")
                  .arg(spin ? spin->value() : -1).arg(words2 ? words2->text() : QString())
                  .arg(pre ? pre->text() : QString()).arg(on && on->isChecked() ? 1 : 0),
              QStringLiteral("17 | 女装 | https://mine.example/ | 1"));
    }

    // ---- 页面：控件存在不算，要能从布局走到（配置页那一栏栽过）----
    LinuxdoPage page;
    page.setAttribute(Qt::WA_DontShowOnScreen, true);
    page.resize(760, 520);
    page.show();
    QCoreApplication::processEvents();
    const auto inLayout = [&page](QWidget* w) {
        bool found = false;
        std::function<void(QLayout*)> walk = [&](QLayout* l) {
            if (!l || found)
                return;
            for (int i = 0; i < l->count(); ++i) {
                QLayoutItem* it = l->itemAt(i);
                if (!it)
                    continue;
                if (QWidget* cw = it->widget()) {
                    if (cw == w) { found = true; return; }
                    walk(cw->layout());
                } else if (QLayout* sub = it->layout())
                    walk(sub);
            }
        };
        walk(page.layout());
        return found;
    };
    const auto names = { "linuxdoEnabled", "linuxdoMinutes", "linuxdoWords", "linuxdoPrefix",
                         "linuxdoNow", "linuxdoHits", "linuxdoStatus", "linuxdoOpen",
                         "linuxdoNote" };
    int laid = 0, found = 0;
    for (const char* n : names) {
        auto* w2 = page.findChild<QWidget*>(QString::fromLatin1(n));
        if (!w2)
            continue;
        ++found;
        if (inLayout(w2))
            ++laid;
    }
    check("九件控件都在（开关/间隔/关键词/前缀/立即查/列表/状态/打开/说明）",
          QStringLiteral("%1/%2").arg(found).arg(laid), QStringLiteral("9/9"));
    auto* status = page.findChild<QLabel*>(QStringLiteral("linuxdoStatus"));
    check("状态行说的就是引擎那一句（不另编一份）", status ? status->text() : QString(),
          w.statusLine());
    auto* hits = page.findChild<QListWidget*>(QStringLiteral("linuxdoHits"));
    check("命中列表初始是空的（这一路一次网都没上）", QString::number(hits ? hits->count() : -1), "0");
    auto* now = page.findChild<QPushButton*>(QStringLiteral("linuxdoNow"));
    check("「立即查一次」没被按下去过（自测不发网络请求）",
          now && now->isEnabled() ? "yes" : "no", "yes");
    page.hide();

    std::printf("linuxdo: 失败 %d 项\n", bad);
    std::fflush(stdout);
    return bad;
}

// --linuxdonet [秒]：真取回一趟（要联网，不进 CI）。这是这一路唯一"拿到了真帖子"的证据 ——
// 上面那 33 条全是喂 fixture 的纯函数判据，它们绿不代表中转还活着。
// 跑在沙箱 APPDATA 里（main.cpp 那份名单），所以它种下的 lastSeen 不会碰到他真实的那份，
// 也正因为是沙箱第一趟，"第一次不弹框"那条能在这里对着真数据验一遍。
extern "C" Q_DECL_EXPORT int ltoolLinuxdoNetRun(int seconds)
{
    int bad = 0;
    const auto check = [&bad](const char* name, const QString& got, const QString& want) {
        const bool ok = got == want;
        if (!ok)
            ++bad;
        std::printf("%s %s got=[%s] want=[%s]\n", ok ? "PASS" : "FAIL", name,
                    got.toUtf8().constData(), want.toUtf8().constData());
        std::fflush(stdout);
    };
    LinuxdoWatch& w = LinuxdoWatch::instance();
    QEventLoop loop;
    QObject::connect(&w, &LinuxdoWatch::changed, &loop, [&w, &loop] {
        if (!w.busy())
            loop.quit();
    });
    QTimer::singleShot(qMax(10, seconds) * 1000, &loop, &QEventLoop::quit);
    w.checkNow();
    loop.exec();
    std::printf("INFO 状态行：%s\n", w.statusLine().toUtf8().constData());
    std::fflush(stdout);
    check("这一趟收回来了（没卡在忙上）", w.busy() ? "yes" : "no", "no");
    check("真的解出了帖子（中转还活着）", w.lastTopicCount() > 0 ? "yes" : "no", "yes");
    check("状态行说出了这次实际走的是哪条",
          w.statusLine().contains(QStringLiteral("直连")) || w.statusLine().contains(QStringLiteral("中转"))
              ? "yes" : "no", "yes");
    check("lastSeen 记下了这一页里最大的 id", w.lastSeenId() > 0 ? "yes" : "no", "yes");
    check("第一趟只播种，不弹框也不攒命中", w.hits().isEmpty() ? "yes" : "no", "yes");
    check("状态行把「第一次」这件事写在脸上",
          w.statusLine().contains(QStringLiteral("第一次")) ? "yes" : "no", "yes");
    std::printf("linuxdonet: 失败 %d 项\n", bad);
    std::fflush(stdout);
    return bad;
}
