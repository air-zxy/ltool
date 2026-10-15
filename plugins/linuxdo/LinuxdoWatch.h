#pragma once

#include <QObject>
#include <QStringList>
#include <QVector>

// linux.do 盯帖的引擎：每隔几分钟拉一次「最新帖」，比标题和标签，命中就在托盘冒一句。
//
// 为什么引擎在这里、页面上面只管配置：这一路必须**没打开过页面也在跑**，
// 而插件的页是懒创建的（点开卡片才建），所以它靠 plugin.json 里 "Activate":true
// 挂牌，启动时由 PluginHost::startBackground() 按名字叫起来（见 PluginHost.h 那段注释）。
// 和 AdBlocker / TaskScheduler 是同一个形状：单例 + start() 幂等 + 配置落 config.ini。
//
// 取回这条路是实测出来的，不是猜的（2026-10-15）：
//  - 直连 linux.do 在这台机器上 DNS 被污染（解到 Facebook 的 IP），必然超时；
//  - 走系统代理能到 Cloudflare，但非浏览器客户端一律吃 "Just a moment" 挑战页 ——
//    带着浏览器导出的 cf_clearance + 完整 Chrome 请求头也一样（它绑 TLS 指纹），
//    所以**给 cookie 也给不出登录态**，这一路看到的是登出态的公开内容；
//  - `https://r.jina.ai/<原地址>` + `X-Return-Format: text` 经同一份代理能拿到真 JSON
//    （实测 latest 30 条 / categories 12 个分类 / 单个分类流 30 条，4.5~7.3 秒一趟）。
//  于是顺序是：先短超时试直连（哪天网络变了就自动用上的那条），再走前缀中转。
//  前缀在配置里可换 —— 谁想挂自己的中转，填进去就行，留空 = 只用直连。
//
// 发帖速率实测 0.64 帖/分钟（一页 30 条跨 47 分钟），所以 5~30 分钟一轮都漏不掉；
// 间隔下限卡 3 分钟，别拿公开 API 当 DDoS 使。

class QTimer;

struct LinuxdoTopic {
    qint64 id = 0;
    QString title;
    QStringList tags;
    QString createdAt;        // 原样留着（ISO8601 UTC），界面只贴日期那半截
    int categoryId = 0;
    QString url;              // https://linux.do/t/<id>，Discourse 自己会跳到带 slug 的正式地址
};

struct LinuxdoHit {
    LinuxdoTopic topic;
    QString matched;          // 命中的那个词（界面上要说清为什么是它）
};

namespace LinuxdoWatch_ {

// 纯函数三件套 —— 判据直接打这三个，不碰网络也不碰托盘（自测一次气泡都不许弹出去）
QVector<LinuxdoTopic> parseTopics(const QByteArray& json);
QVector<LinuxdoHit> matchNew(const QVector<LinuxdoTopic>& topics, const QStringList& keywords,
                             qint64 lastSeen);
QString relayUrl(const QString& prefix, const QString& origin);
// 要不要弹那一句：第一次跑只记下"从哪条开始"，不弹（装上就哗啦十几条没人会看）；
// 没命中也不弹。判据打这一个函数，页面和引擎共用同一条决定。
bool shouldNotify(bool firstRun, int freshCount);

} // namespace LinuxdoWatch_

class LinuxdoWatch : public QObject {
    Q_OBJECT
public:
    static LinuxdoWatch& instance();

    // 幂等：start() 只建定时器 + 从配置回读（AdBlocker 同一条规矩）
    void start();
    void stop();

    bool enabled() const { return m_enabled; }
    int intervalMinutes() const { return m_minutes; }
    QStringList keywords() const { return m_keywords; }
    QString prefix() const { return m_prefix; }
    qint64 lastSeenId() const { return m_lastSeen; }
    QString lastCheckText() const { return m_lastCheck; }
    QString statusLine() const { return m_status; }
    QVector<LinuxdoHit> hits() const { return m_hits; }
    bool busy() const { return m_busy; }
    int lastTopicCount() const { return m_lastCount; }   // 上一次一趟拿到几条帖子

    // 改配置：写盘 + 重排定时器 + 立刻生效（页面那几颗控件走的就是这一条）
    void setEnabled(bool on);
    void setIntervalMinutes(int minutes);
    void setKeywords(const QStringList& words);
    void setPrefix(const QString& prefix);

    // 查一次（异步）。返回的是"这次有没有把这一趟发出去"，结果一律走 changed()/statusLine()。
    bool checkNow();

signals:
    void changed();

private:
    explicit LinuxdoWatch(QObject* parent = nullptr);
    void load();
    void save() const;
    void arm();
    void fetchVia(int stage);                       // 0=直连 1=前缀中转
    void onBody(int stage, const QByteArray& body, const QString& how, qint64 ms);

    QTimer* m_timer = nullptr;
    bool m_enabled = true;
    int m_minutes = 10;
    QStringList m_keywords { QStringLiteral("福利"), QStringLiteral("女装"),
                             QStringLiteral("公益") };
    QString m_prefix = QStringLiteral("https://r.jina.ai/");
    qint64 m_lastSeen = 0;
    QString m_lastCheck;
    QString m_status = QStringLiteral("还没查过");
    QVector<LinuxdoHit> m_hits;
    bool m_busy = false;
    bool m_booted = false;
    int m_lastCount = 0;
};
