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
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <functional>
#include <memory>

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
        // 底部这些按钮是"要几颗加几颗"：装了东西的那张卡现在同时给「禁用」和「卸载」，
        // 可更新那张还要加一颗「更新」—— 写死两颗很快就会不够用
        m_foot = new QHBoxLayout;
        m_foot->setSpacing(6);
        m_foot->addWidget(m_state, 1);
        m_foot->addStretch(0);   // 这个 stretch 是插按钮的锚点：状态词占满左边，按钮排在右

        text->addWidget(name);
        text->addWidget(m_desc);
        text->addLayout(m_foot);
        lay->addWidget(icon, 0, Qt::AlignTop);
        lay->addLayout(text, 1);
    }

    QLabel* stateLabel() const { return m_state; }
    QPushButton* addButton(const QString& text)
    {
        auto* b = new QPushButton(text, this);
        b->setCursor(Qt::PointingHandCursor);
        b->setMaximumWidth(72);
        m_foot->insertWidget(m_foot->count() - 1, b);   // 插在那个 stretch 之前
        return b;
    }
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
    QHBoxLayout* m_foot = nullptr;
    std::function<void()> m_onClick;
};

// 装了的东西既能禁用也能卸载，所以一张卡上最多三颗按钮。enableIt=true 就是那颗「启用」
void addToggleButton(ToolsPage* page, ShopCard* card, const QString& file, bool enableIt)
{
    QPushButton* btn = card->addButton(enableIt ? QStringLiteral("启用") : QStringLiteral("禁用"));
    QObject::connect(btn, &QPushButton::clicked, page, [page, file, card, enableIt] {
        const QString err = PluginStore::setEnabled(file, enableIt);
        card->stateLabel()->setText(err.isEmpty()
                                        ? QStringLiteral("已%1 · 下次启动加载它")
                                              .arg(enableIt ? QStringLiteral("启用") : QStringLiteral("禁用"))
                                        : err);
        Utils::log(QStringLiteral("store: %1 %2%3")
                       .arg(enableIt ? QStringLiteral("启用") : QStringLiteral("禁用"), file,
                            err.isEmpty() ? QString() : QStringLiteral(" 失败：") + err));
        if (err.isEmpty())
            page->rebuildGrid();
    });
}

// 卸载 = 真把那个文件从目录里删掉。删得掉就当场没了；这个进程已经加载过它的话 Windows 不放行，
// PluginStore 把它记进待删表、下次启动第一件事删掉 —— 那时候这句状态就该说"下次启动生效"。
void addUninstallButton(ToolsPage* page, ShopCard* card, const QString& name, const QString& file,
                        const QString& again)
{
    QPushButton* btn = card->addButton(QStringLiteral("卸载"));
    QObject::connect(btn, &QPushButton::clicked, page, [page, card, name, file, again] {
        const auto answer = QMessageBox::question(
            page, QStringLiteral("卸载插件"),
            QStringLiteral("这会把 %1 从\n%2\n删掉（配置和数据都不动）。%3\n\n确定吗？")
                .arg(file, PluginHost::pluginsDir(), again),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);   // 默认停在"不"
        if (answer != QMessageBox::Yes) {
            card->stateLabel()->setText(QStringLiteral("没动。"));
            return;
        }
        const QString res = PluginStore::uninstall(file);
        if (res == QLatin1String("pending")) {
            card->stateLabel()->setText(QStringLiteral("已安排卸载 · 这个进程还加载着它，下次启动真删掉"));
            page->rebuildGrid();
            return;
        }
        if (!res.isEmpty()) {
            card->stateLabel()->setText(res);
            return;
        }
        Utils::log(QStringLiteral("store: 卸载 %1（%2）完成，重扫目录").arg(file, name));
        PluginHost::reload();
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

// "卸了之后还能不能装回来"只有拉过清单才答得上来；没拉过就别假装有把握
QString ToolsPage::reinstallHint(const PluginStore::Row& r) const
{
    if (!m_haveStore)
        return QStringLiteral("还没拉过清单，不确定仓库里有没有它 —— 删了可能要重装才回得来。");
    if (r.state == PluginStore::State::LocalOnly)
        return QStringLiteral("插件清单里没列它（本机独有的那几个）—— 删了要等仓库收了它才装得回来。");
    return QStringLiteral("清单里有它，卸完还能从这一面再装回来。");
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
        auto* install = card->addButton(r.state == S::Missing ? QStringLiteral("装")
                                                               : QStringLiteral("更新"));
        connect(install, &QPushButton::clicked, this, [this, e, card, install] {
            install->setEnabled(false);
            installEntry(e, install, card->stateLabel());
        });
        if (r.state == S::Update) {
            // 本机确实装着它（只是哈希不同）：禁得掉、也卸得掉，不能只剩"更新"这一条路
            addToggleButton(this, card, file, /*enableIt=*/false);
            addUninstallButton(this, card, e.name, file,
                                QStringLiteral("清单里有它，卸完还能从这一面再装回来。"));
        }
        if (r.state == S::Missing) {
            // 还没装的那张：点正文就是按同一颗按钮。这里绝不能去 showTool —— 本机没那个 dll
            card->setBodyClick([install] { install->click(); });
        } else {
            // 「可更新」是装了但哈希不同：那张页本来就能看，正文照常打开，更新交给按钮
            card->setBodyClick(openable ? [this, id] { showTool(id); } : std::function<void()>());
        }
        break;
    }
    case S::Disabled:
        // .off 那个宿主本来就不会去加载它：能启用、能卸载，点正文没有页可看
        addToggleButton(this, card, file, /*enableIt=*/true);
        addUninstallButton(this, card, e.name, file, reinstallHint(r));
        card->setBodyClick(std::function<void()>());
        break;
    case S::Installed:
    case S::LocalOnly:
        addToggleButton(this, card, file, /*enableIt=*/false);
        addUninstallButton(this, card, e.name, file, reinstallHint(r));
        card->setBodyClick(openable ? [this, id] { showTool(id); } : std::function<void()>());
        break;
    case S::ApiMismatch:
    case S::Blocked:
        // 本机没有能用的那个文件：一颗按钮都不给，为什么不行写在状态那行里
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

    // 镜像 → 本地代理 → 直连，逐条试（那条顺序只在 PluginStore::fetch 里写一遍，两段联网共用）
    PluginStore::fetch(url, this, {}, {}, {}, [this, url](const PluginStore::Fetched& got) {
        m_storeBusy = false;
        if (!got.ok) {
            // 拉不到就退回"只列本机装着的那些"，并把这件事说出来 —— 不能拿一张半截的列表当结论
            m_haveStore = false;
            m_lastEntries.clear();
            m_lastProblems.clear();
            m_storeStatus->setText(QStringLiteral("清单没拉到：%1 —— 这一面退回本机装着的那几个，它们照常能用")
                                       .arg(got.err));
            rebuildGrid();
            Utils::log(QStringLiteral("store: 拉清单失败 %1 url=%2").arg(got.err, url));
            return;
        }
        QStringList problems;
        const auto entries = PluginStore::parseIndex(got.body, &problems);
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
        QString line = QStringLiteral("清单 %1 条（%2 · 走的是 %3，一共试了 %4 次）· 这一面 %5 张卡"
                                      " · 能装或可更新 %6 · 拒收 %7 · 清单外 %8"
                                      " —— 装了的点正文是打开，没装的点正文是装")
                          .arg(entries.size()).arg(url, got.via).arg(got.tries).arg(all.size())
                          .arg(canInstall).arg(problems.size()).arg(outside);
        if (!problems.isEmpty())
            line += QStringLiteral("（拒收：%1）").arg(problems.join(QStringLiteral("；")));
        m_storeStatus->setText(line);
        rebuildGrid();
        Utils::log(QStringLiteral("store: 清单 %1 条（%2 拿到），卡片 %3 张，能装 %4，拒收 %5，清单外 %6")
                       .arg(entries.size()).arg(got.via).arg(all.size()).arg(canInstall)
                       .arg(problems.size()).arg(outside));
    });
}

void ToolsPage::installEntry(const PluginStore::Entry& e, QPushButton* btn, QLabel* state)
{
    const QString dir = PluginHost::pluginsDir();
    QDir().mkpath(dir);
    const QString part = dir + QLatin1Char('/') + e.file + QStringLiteral(".part");
    auto out = std::make_shared<QFile>(part);
    auto writeFailed = std::make_shared<bool>(false);

    PluginStore::fetch(e.url, this,
                       [out, part, writeFailed] {
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
                       [btn](qint64 got, qint64 all) {
                           // 就地改按钮上那行字，不新插一块进度条：版面一跳就是"突然出现在上面"
                           btn->setText(all > 0 ? QStringLiteral("%1%").arg(got * 100 / all)
                                                : QStringLiteral("%1 KB").arg(got / 1024));
                       },
                       [this, e, btn, state, part, out, writeFailed](const PluginStore::Fetched& got) {
                           if (out->isOpen())
                               out->close();
                           btn->setEnabled(true);
                           if (!got.ok || *writeFailed) {
                               QFile::remove(part);
                               state->setText(got.err.isEmpty()
                                                  ? QStringLiteral("没下完：本地写不进 .part")
                                                  : QStringLiteral("没下完：%1").arg(got.err));
                               btn->setText(QStringLiteral("重试"));
                               Utils::log(QStringLiteral("store: 下 %1 没成：%2").arg(e.file, got.err));
                               return;
                           }
                           const QString err = PluginStore::finishDownload(part, e);
                           if (!err.isEmpty()) {
                               state->setText(err);
                               btn->setText(QStringLiteral("重试"));
                               Utils::log(QStringLiteral("store: 装 %1 没成：%2").arg(e.file, err));
                               return;
                           }
                           Utils::log(QStringLiteral("store: 装好 %1（%2 拿到，哈希对过，%3 B）")
                                          .arg(e.file, got.via).arg(e.bytes));
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
