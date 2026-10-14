#include "ToolsPage.h"

#include "DevEnv.h"
#include "PluginHost.h"
#include "PluginStore.h"
#include "Utils.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <functional>

namespace {

constexpr int kGridColumns = 3;

// 清单里那个 color 是 "r,g,b" 三个数（plugin.json 里就这么写），宿主扫出来的那份是 QColor。
// 认不出来就按 id 派生一个稳的颜色 —— 不能因为少一个字段画出一张黑卡。
QColor colorOf(const QString& spec, const QString& id)
{
    const QStringList parts = spec.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (parts.size() == 3) {
        bool ok = false;
        const int r = parts[0].trimmed().toInt(&ok);
        if (ok) {
            QColor c(r, parts[1].trimmed().toInt(), parts[2].trimmed().toInt());
            if (c.isValid())
                return c;
        }
    }
    const uint seed = qHash(id);
    return QColor(90 + int(seed % 120), 90 + int((seed / 7) % 120), 110 + int((seed / 13) % 110));
}

// 一张商店卡片：图标 + 名字 + 说明 + 底下那行「状态 / 按钮」。
class ShopCard : public QFrame {
public:
    ShopCard(const QString& title, const QString& desc, const QColor& color, QWidget* parent)
        : QFrame(parent)
    {
        // 还是叫 "row"：`--plugintest` 数卡片就是数这个名字的 QFrame（插件数 = 卡片数）
        setObjectName(QStringLiteral("row"));
        setAttribute(Qt::WA_StyledBackground);
        setMinimumHeight(96);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(12, 10, 12, 10);
        lay->setSpacing(10);

        auto* icon = new QLabel(this);
        icon->setFixedSize(32, 32);
        icon->setPixmap(Utils::letterIcon(title, color).pixmap(32, 32));
        icon->setScaledContents(true);
        icon->setAttribute(Qt::WA_TranslucentBackground);

        auto* text = new QVBoxLayout;
        text->setSpacing(2);
        auto* name = new QLabel(title, this);
        QFont f = name->font();
        f.setBold(true);
        name->setFont(f);
        m_desc = new QLabel(desc, this);
        m_desc->setObjectName(QStringLiteral("muted"));
        m_desc->setWordWrap(true);

        m_state = new QLabel(this);
        m_state->setObjectName(QStringLiteral("muted"));
        m_state->setWordWrap(true);
        m_action = new QPushButton(this);
        m_action->setCursor(Qt::PointingHandCursor);
        m_action->setMaximumWidth(88);
        m_secondary = new QPushButton(this);
        m_secondary->setCursor(Qt::PointingHandCursor);
        m_secondary->setMaximumWidth(88);
        m_secondary->setVisible(false);
        auto* foot = new QHBoxLayout;
        foot->setSpacing(8);
        foot->addWidget(m_state, 1);
        foot->addWidget(m_secondary, 0, Qt::AlignRight);
        foot->addWidget(m_action, 0, Qt::AlignRight);

        text->addWidget(name);
        text->addWidget(m_desc);
        text->addLayout(foot);
        lay->addWidget(icon, 0, Qt::AlignTop);
        lay->addLayout(text, 1);
    }

    QLabel* stateLabel() const { return m_state; }
    QPushButton* actionButton() const { return m_action; }
    // 第二颗：「可更新」那种卡要同时给「更新」和「禁用」—— 装了的东西不能因为版本旧就禁不掉
    QPushButton* secondaryButton() const { return m_secondary; }
    void setBodyClick(std::function<void()> fn)
    {
        m_onClick = std::move(fn);
        // 点不动的那张卡（没页可看、又没按钮可按）不该画成一只手
        setCursor(m_onClick ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }

protected:
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && rect().contains(e->pos()) && m_onClick)
            m_onClick();
        QFrame::mouseReleaseEvent(e);
    }

private:
    QLabel* m_desc = nullptr;
    QLabel* m_state = nullptr;
    QPushButton* m_action = nullptr;
    QPushButton* m_secondary = nullptr;
    std::function<void()> m_onClick;
};

// 给这张卡挂一颗「禁用」。asPrimary=false 时挂到第二颗上 ——
// 「可更新」那张卡本来要按「更新」，但装了的东西不能因为版本旧就禁不掉。
void addDisableButton(ToolsPage* page, ShopCard* card, const QString& file, bool asPrimary = true)
{
    QPushButton* btn = asPrimary ? card->actionButton() : card->secondaryButton();
    btn->setVisible(true);
    btn->setText(QStringLiteral("禁用"));
    QObject::connect(btn, &QPushButton::clicked, page, [page, file, card] {
        const QString err = PluginStore::setEnabled(file, false);
        card->stateLabel()->setText(err.isEmpty()
                                        ? QStringLiteral("已禁用 · 下次启动不再加载它")
                                        : err);
        Utils::log(QStringLiteral("store: 禁用 %1%2").arg(file,
                    err.isEmpty() ? QString() : QStringLiteral(" 失败：") + err));
        if (err.isEmpty())
            page->rebuildGrid();
    });
}

} // namespace

ToolsPage::ToolsPage(QWidget* parent)
    : QWidget(parent)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(12, 6, 4, 4);
    lay->setSpacing(8);

    auto* head = new QHBoxLayout;
    head->setSpacing(8);
    m_back = new QToolButton(this);
    m_back->setText(QStringLiteral("‹ 返回商店"));
    m_back->setAutoRaise(true);
    m_back->setCursor(Qt::PointingHandCursor);
    m_back->setVisible(false);
    m_heading = new QLabel(QStringLiteral("插件商店"), this);
    m_heading->setObjectName(QStringLiteral("pageTitle"));
    auto* refresh = new QPushButton(QStringLiteral("刷新清单"), this);
    refresh->setObjectName(QStringLiteral("storeRefresh"));
    refresh->setCursor(Qt::PointingHandCursor);
    refresh->setToolTip(QStringLiteral("拉插件仓库那份 index.json。\n"
                                      "地址在配置页「插件清单地址」那一行，留空 = 内置那条 raw 地址；代理沿用「下载代理」那一行。"));
    head->addWidget(m_back);
    head->addWidget(m_heading);
    head->addStretch(1);
    head->addWidget(refresh);
    lay->addLayout(head);

    m_storeStatus = new QLabel(QStringLiteral(
        "这一面列的是本机 `ltool-plugins\\` 里装着的那几个。点右上「刷新清单」，仓库里能装的也进这同一面。"), this);
    m_storeStatus->setObjectName(QStringLiteral("tip"));
    m_storeStatus->setWordWrap(true);
    lay->addWidget(m_storeStatus);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(buildGrid());     // 0 = 那一面卡片，插件页往后追加
    lay->addWidget(m_stack, 1);

    connect(m_back, &QToolButton::clicked, this, [this] {
        m_stack->setCurrentIndex(0);
        m_back->setVisible(false);
        m_heading->setText(QStringLiteral("插件商店"));
    });
    connect(refresh, &QPushButton::clicked, this, [this] { refreshStore(); });
}

QVector<PluginStore::Row> ToolsPage::shopRows() const
{
    // 一条列表、一条规则，绝不出现"墙是一批、清单是另一批"：
    //   · 没拉过清单 → 只报目录里有什么（不猜仓库里有什么）
    //   · 拉过 → 以清单为准，后面跟上本机装着而清单没列的那几个
    if (!m_haveStore)
        return PluginStore::localRows(QString());
    QVector<PluginStore::Row> out = PluginStore::rows(m_lastEntries);
    const QVector<PluginStore::Row> extra = PluginStore::localOnly(m_lastEntries);
    for (const PluginStore::Row& r : extra)
        out.append(r);
    return out;
}

QWidget* ToolsPage::buildGrid()
{
    auto* page = new QWidget(this);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(8);

    const QVector<PluginStore::Row> rows = shopRows();
    if (rows.isEmpty()) {
        auto* empty = new QLabel(QStringLiteral(
            "插件目录 %1 里一个 dll 都没有，清单也没拉到 —— 这一面没东西可列。"
            "重装一次 ltool，或者点「刷新清单」。").arg(PluginHost::pluginsDir()), page);
        empty->setObjectName(QStringLiteral("tip"));
        empty->setWordWrap(true);
        lay->addWidget(empty);
        lay->addStretch(1);
        return page;
    }

    auto* grid = new QGridLayout;
    grid->setSpacing(8);
    for (int i = 0; i < rows.size(); ++i) {
        grid->addWidget(makeCard(rows[i]), i / kGridColumns, i % kGridColumns);
        grid->setColumnStretch(i % kGridColumns, 1);
    }
    lay->addLayout(grid);
    lay->addStretch(1);
    return page;
}

QWidget* ToolsPage::makeCard(const PluginStore::Row& r)
{
    auto* card = new ShopCard(r.remote.name, r.remote.desc, colorOf(r.remote.color, r.remote.id), this);
    card->stateLabel()->setText(r.note.isEmpty()
                                    ? PluginStore::stateWord(r.state)
                                    : QStringLiteral("%1 · %2").arg(PluginStore::stateWord(r.state), r.note));

    using S = PluginStore::State;
    const PluginStore::Entry e = r.remote;
    const QString id = r.remote.id;
    const QString file = r.remote.file;
    // "能不能点开"看的不是状态词，是宿主认不认得出那个 dll：
    // 已装但没通过检查的那条也得老实承认打不开，不能给一只手形然后什么都发生不了。
    const PluginHost::Meta* md = PluginHost::meta(id);
    const bool openable = md && md->ok();
    const bool installed = r.state == S::Installed || r.state == S::Update || r.state == S::LocalOnly;

    switch (r.state) {
    case S::Missing:
    case S::Update: {
        card->actionButton()->setText(r.state == S::Missing ? QStringLiteral("装")
                                                             : QStringLiteral("更新"));
        connect(card->actionButton(), &QPushButton::clicked, this, [this, e, card] {
            card->actionButton()->setEnabled(false);
            installEntry(e, card->actionButton(), card->stateLabel());
        });
        if (r.state == S::Update)
            addDisableButton(this, card, file, /*asPrimary=*/false);   // 装了但哈希不同：更新 + 禁用都给
        if (r.state == S::Missing) {
            // 还没装的那张：点正文就是按同一颗按钮。这里绝不能去 showTool —— 本机没那个 dll
            card->setBodyClick([card] { card->actionButton()->click(); });
        } else {
            // 「可更新」是装了但哈希不同：那张页本来就能看，正文照常打开，更新交给按钮
            card->setBodyClick(openable ? [this, id] { showTool(id); } : std::function<void()>());
        }
        break;
    }
    case S::Disabled: {
        card->actionButton()->setText(QStringLiteral("启用"));
        connect(card->actionButton(), &QPushButton::clicked, this, [this, file, card] {
            const QString err = PluginStore::setEnabled(file, true);
            card->stateLabel()->setText(err.isEmpty()
                                            ? QStringLiteral("已启用 · 下次启动加载它")
                                            : err);
            Utils::log(QStringLiteral("store: 启用 %1%2").arg(file,
                        err.isEmpty() ? QString() : QStringLiteral(" 失败：") + err));
            if (err.isEmpty())
                rebuildGrid();
        });
        // .off 那个宿主不会去加载，所以这张卡点正文没有页可看
        card->setBodyClick(std::function<void()>());
        break;
    }
    case S::Installed:
    case S::LocalOnly: {
        addDisableButton(this, card, file);
        card->setBodyClick(openable ? [this, id] { showTool(id); } : std::function<void()>());
        break;
    }
    case S::ApiMismatch:
    case S::Blocked:
        // 装不了 / 条目不能用：不给按钮，为什么不行就写在状态那行里
        card->actionButton()->setVisible(false);
        card->setBodyClick(std::function<void()>());
        break;
    }
    if (installed && !openable)
        card->stateLabel()->setText(QStringLiteral("%1 · 本机有这个 dll 但没通过检查：%2")
                                        .arg(PluginStore::stateWord(r.state),
                                             PluginHost::meta(id) ? QString()
                                                    : QStringLiteral("目录里认不出它")));
    return card;
}

void ToolsPage::refreshStore()
{
    if (m_storeBusy)
        return;
    m_storeBusy = true;
    const QString url = PluginStore::indexUrl();
    m_storeStatus->setText(QStringLiteral("正在拉清单：%1").arg(url));

    // 管理器挂在这页上：页（连带设置窗）销毁时 reply 一起断，回调里再用 this 已经被 Qt 摘掉
    auto* nam = new QNetworkAccessManager(this);
    DevEnv::applyProxy(nam);
    QNetworkRequest req { QUrl(url) };
    req.setTransferTimeout(20000);
    QNetworkReply* reply = nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, url] {
        m_storeBusy = false;
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        nam->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            // 拉不到就退回"只列本机装着的那些"，并把这件事说出来 —— 不能拿一张半截的列表当结论
            m_haveStore = false;
            m_lastEntries.clear();
            m_lastProblems.clear();
            m_storeStatus->setText(QStringLiteral("清单没拉到：%1%2 —— 这一面退回本机装着的那几个，它们照常能用")
                                       .arg(reply->errorString(),
                                            http ? QStringLiteral("（HTTP %1）").arg(http) : QString()));
            rebuildGrid();
            Utils::log(QStringLiteral("store: 拉清单失败 http=%1 err=%2 url=%3").arg(http)
                           .arg(reply->errorString(), url));
            return;
        }
        QStringList problems;
        const auto entries = PluginStore::parseIndex(body, &problems);
        m_haveStore = true;
        m_lastEntries = entries;
        m_lastProblems = problems;

        const QVector<PluginStore::Row> all = shopRows();
        int canInstall = 0;
        for (const PluginStore::Row& r : all) {
            if (r.state == PluginStore::State::Missing || r.state == PluginStore::State::Update)
                ++canInstall;
        }
        const int outside = PluginStore::localOnly(entries).size();
        QString line = QStringLiteral("清单 %1 条（地址 %2）· 这一面 %3 张卡 · 能装或可更新 %4 · 拒收 %5 · 清单外 %6"
                                      " —— 装了的点正文是打开，没装的点正文是装")
                          .arg(entries.size()).arg(url).arg(all.size())
                          .arg(canInstall).arg(problems.size()).arg(outside);
        if (!problems.isEmpty())
            line += QStringLiteral("（拒收：%1）").arg(problems.join(QStringLiteral("；")));
        m_storeStatus->setText(line);
        rebuildGrid();
        Utils::log(QStringLiteral("store: 清单 %1 条，卡片 %2 张，能装 %3，拒收 %4，清单外 %5")
                       .arg(entries.size()).arg(all.size()).arg(canInstall)
                       .arg(problems.size()).arg(outside));
    });
}

void ToolsPage::installEntry(const PluginStore::Entry& e, QPushButton* btn, QLabel* state)
{
    const QString dir = PluginHost::pluginsDir();
    QDir().mkpath(dir);
    const QString part = dir + QLatin1Char('/') + e.file + QStringLiteral(".part");
    auto* out = new QFile(part);
    if (!out->open(QIODevice::WriteOnly)) {
        state->setText(QStringLiteral("写不下去：%1").arg(out->errorString()));
        btn->setEnabled(true);
        btn->setText(QStringLiteral("重试"));
        out->deleteLater();
        Utils::log(QStringLiteral("store: %1 的 .part 写不下去：%2").arg(e.file, out->errorString()));
        return;
    }
    auto* nam = new QNetworkAccessManager(this);
    DevEnv::applyProxy(nam);
    QNetworkRequest req { QUrl(e.url) };
    req.setTransferTimeout(60000);
    QNetworkReply* reply = nam->get(req);
    connect(reply, &QNetworkReply::downloadProgress, this, [btn](qint64 got, qint64 all) {
        // 就地改按钮上那行字，不新插一块进度条：版面一跳就是"突然出现在上面"
        btn->setText(all > 0 ? QStringLiteral("%1%").arg(got * 100 / all)
                             : QStringLiteral("%1 KB").arg(got / 1024));
    });
    // readyRead 而不是 finished 之后一次 readAll：dll 几百 KB 到几 MB，一次读进来白占内存
    connect(reply, &QNetworkReply::readyRead, this, [reply, out] { out->write(reply->readAll()); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, out, e, btn, state, part, dir] {
        out->close();
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString netErr = reply->error() != QNetworkReply::NoError ? reply->errorString() : QString();
        reply->deleteLater();
        nam->deleteLater();
        out->deleteLater();
        if (!netErr.isEmpty()) {
            QFile::remove(part);
            state->setText(QStringLiteral("没下完：%1%2").arg(netErr,
                         http ? QStringLiteral("（HTTP %1）").arg(http) : QString()));
            btn->setEnabled(true);
            btn->setText(QStringLiteral("重试"));
            Utils::log(QStringLiteral("store: 下 %1 失败 http=%2 err=%3").arg(e.file).arg(http).arg(netErr));
            return;
        }
        const QString err = PluginStore::finishDownload(part, e, dir);
        if (!err.isEmpty()) {
            state->setText(err);
            btn->setEnabled(true);
            btn->setText(QStringLiteral("重试"));
            Utils::log(QStringLiteral("store: 装 %1 没成：%2").arg(e.file, err));
            return;
        }
        Utils::log(QStringLiteral("store: 装好 %1（哈希对过，%2 B）").arg(e.file).arg(e.bytes));
        // 落盘了就重扫整面：这张卡的按钮该变成「禁用」、正文该变回"点开那张页"
        PluginHost::reload();
        rebuildGrid();
    });
}

bool ToolsPage::showTool(const QString& id, const QString& text)
{
    QString err;
    QWidget* w = PluginHost::page(id, this, &err);
    if (!w) {
        m_heading->setText(QStringLiteral("插件商店"));
        Utils::log(QStringLiteral("plugin: 打不开 %1：%2").arg(id, err));
        return false;
    }
    if (m_stack->indexOf(w) < 0)
        m_stack->addWidget(w);
    if (!text.isEmpty() && !PluginHost::feed(id, text))
        Utils::log(QStringLiteral("plugin: %1 没接住那段文本（长度 %2）").arg(id).arg(text.size()));
    m_stack->setCurrentWidget(w);
    m_back->setVisible(true);
    const PluginHost::Meta* md = PluginHost::meta(id);
    m_heading->setText(QStringLiteral("插件商店 · %1").arg(md ? md->name : id));
    return true;
}

void ToolsPage::rebuildGrid()
{
    // 只换卡片那一格，已经点开过的插件页留在 stack 里不动（dll 不卸载，拆了也没意义）
    QWidget* old = m_stack->widget(0);
    m_stack->removeWidget(old);
    old->deleteLater();
    m_stack->insertWidget(0, buildGrid());
    m_back->setVisible(false);
    m_heading->setText(QStringLiteral("插件商店"));
}
