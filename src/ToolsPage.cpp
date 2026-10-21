#include "ToolsPage.h"

#include "ShellMenu.h"

#include "DevEnv.h"
#include "PluginHost.h"
#include "PluginStore.h"
#include "Theme.h"
#include "Utils.h"
#include "platform/ProcInfo.h"

#include <QColor>
#include <QDesktopServices>
#include <QTimer>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QScrollArea>

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
    const size_t seed = qHash(id);
    return QColor(90 + int(seed % 120), 90 + int((seed / 7) % 120), 110 + int((seed / 13) % 110));
}

// 一张商店卡片：图标 + 名字 + 说明 + 状态，操作按钮排在卡片最底下一行。
class ShopCard : public QFrame {
public:
    ShopCard(const QString& title, const QString& desc, const QColor& color, const QString& version, QWidget* parent)
        : QFrame(parent)
    {
        setObjectName(QStringLiteral("row"));
        setAttribute(Qt::WA_StyledBackground);
        setMinimumHeight(128);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(14, 11, 14, 11);
        lay->setSpacing(6);

        auto* topRow = new QHBoxLayout;
        topRow->setSpacing(8);
        topRow->setContentsMargins(0, 0, 0, 0);

        auto* icon = new QLabel(this);
        icon->setFixedSize(32, 32);
        icon->setPixmap(Utils::letterIcon(title, color).pixmap(32, 32));
        icon->setScaledContents(true);
        icon->setAttribute(Qt::WA_TranslucentBackground);
        topRow->addWidget(icon, 0, Qt::AlignVCenter);

        auto* name = new QLabel(title, this);
        QFont f = name->font();
        f.setPointSize(f.pointSize() + 1);
        f.setBold(true);
        name->setFont(f);
        topRow->addWidget(name, 0, Qt::AlignVCenter);

        if (!version.isEmpty()) {
            const QString vText = (version.startsWith(QLatin1Char('v')) || version.startsWith(QLatin1Char('V')))
                                      ? version
                                      : QStringLiteral("v%1").arg(version);
            auto* verBadge = new QLabel(vText, this);
            verBadge->setObjectName(QStringLiteral("verBadge"));
            Theme::bind(verBadge,
                "background: @card; "
                "color: @groupText; "
                "border: 1px solid @cardLine; "
                "border-radius: 4px; "
                "padding: 1px 6px; "
                "font-size: 8.5pt; "
                "font-weight: 500;");
            topRow->addWidget(verBadge, 0, Qt::AlignVCenter);
        }

        topRow->addStretch(1);

        // 可更新那颗卡片的标记：一个绿色向上箭头，钉在右上角。
        m_arrow = new QLabel(QStringLiteral("\u2191"), this);
        Theme::bind(m_arrow,
            "color: @ok; font-size: 14pt; font-weight: 700; background: transparent;");
        m_arrow->setVisible(false);
        topRow->addWidget(m_arrow, 0, Qt::AlignVCenter);

        m_desc = new QLabel(desc, this);
        m_desc->setObjectName(QStringLiteral("muted"));
        m_desc->setWordWrap(true);

        // 状态信息独占一行（整卡宽度），再也不和右下角的 1~3 颗按钮在同一行互挤
        m_state = new QLabel(this);
        m_state->setObjectName(QStringLiteral("muted"));
        m_state->setProperty("shopState", true);
        m_state->setWordWrap(true);

        auto* footRow = new QHBoxLayout;
        footRow->setSpacing(6);
        footRow->setContentsMargins(0, 0, 0, 0);
        footRow->addStretch(1);

        m_foot = new QHBoxLayout;
        m_foot->setSpacing(6);
        m_foot->setContentsMargins(0, 0, 0, 0);
        footRow->addLayout(m_foot, 0);

        lay->addLayout(topRow);
        lay->addWidget(m_desc, 1);
        lay->addWidget(m_state);
        lay->addLayout(footRow);
    }

    void setUpdateArrow(bool on) { if (m_arrow) m_arrow->setVisible(on); }
    QLabel* stateLabel() const { return m_state; }
    QPushButton* addButton(const QString& text, bool isPrimary = false)
    {
        auto* b = new QPushButton(text, this);
        b->setCursor(Qt::PointingHandCursor);
        b->setMinimumWidth(56);
        b->setFixedHeight(30);
        if (isPrimary) {
            b->setObjectName(QStringLiteral("cardBtnPrimary"));
            Theme::bind(b,
                "QPushButton {"
                "  background: @accent;"
                "  border: 1px solid @accent;"
                "  color: @onAccent;"
                "  border-radius: 6px;"
                "  font-size: 9pt;"
                "  font-weight: 600;"
                "  padding: 0 10px;"
                "}"
                "QPushButton:hover { background: @accentUp; border-color: @accentUp; }"
                "QPushButton:pressed { background: @accentDown; }"
                "QPushButton:disabled { background: @field; border: 1px solid @line; color: @disabled; }"
            );
        } else {
            b->setObjectName(QStringLiteral("cardBtn"));
            Theme::bind(b,
                "QPushButton {"
                "  background: @button;"
                "  border: 1px solid @borderHi;"
                "  color: @text;"
                "  border-radius: 6px;"
                "  font-size: 9pt;"
                "  font-weight: 500;"
                "  padding: 0 10px;"
                "}"
                "QPushButton:hover { background: @buttonUp; border-color: @borderHiUp; }"
                "QPushButton:pressed { background: @buttonDown; }"
                "QPushButton:disabled { color: @disabled; background: @field; border-color: @line; }"
            );
        }
        m_foot->addWidget(b);
        return b;
    }
    void setBodyClick(std::function<void()> fn)
    {
        m_onClick = std::move(fn);
        // 点不动的那张卡（没页可看、又没按钮可按）不该画成一只手
        setCursor(m_onClick ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }
    // 判据量的是"这件事到底给没给"（m_onClick 那个决策本身），不是光标形状那种二手证据 ——
    // 他 10-20 报"点不开"那次，卡片状态词是对的、按钮也对，只有正文点了个空函数。
    bool hasBodyClick() const { return static_cast<bool>(m_onClick); }

protected:
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        QFrame::mouseReleaseEvent(e);
        if (e->button() == Qt::LeftButton && rect().contains(e->pos()) && m_onClick)
            m_onClick();
    }

private:
    QLabel* m_desc = nullptr;
    QLabel* m_arrow = nullptr;
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
        if (err.isEmpty()) {
            PluginHost::reload();
            ShellMenu::syncWithPlugin();     // 禁用「右键动作」= 右键里那几项也该没了
            QTimer::singleShot(0, page, [page] { page->rebuildGrid(); });
        }
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
        QTimer::singleShot(0, page, [page, file, name] {
            page->applyUninstall(file, name, nullptr);
        });
    });
}


} // namespace

// 答了"是"之后那一段。单独拿出来是因为离屏判据跑不到 `answer == Yes` 那一支 ——
// 无窗口平台上 question() 会立刻返回默认值（"不"），所以只能让测试直接调这一段。
// 走的是同一份代码，不是照着抄的第二遍。
void ToolsPage::applyUninstall(const QString& file, const QString& name, QLabel* state)
{
    const QString res = PluginStore::uninstall(file);
    if (res == QLatin1String("pending")) {
        if (state)
            state->setText(QStringLiteral("已安排卸载 · 这个进程还加载着它，下次启动真删掉"));
        rebuildGrid();
        return;
    }
    if (!res.isEmpty()) {
        if (state)
            state->setText(res);
        return;
    }
    Utils::log(QStringLiteral("store: 卸载 %1（%2）完成，重扫目录").arg(file, name));
    PluginHost::reload();
    ShellMenu::syncWithPlugin();             // 卸载了「右键动作」，文件夹右键里就不该再有 ltool
    rebuildGrid();
}


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
    m_back->setToolTip(QStringLiteral("回到那一面卡片墙（在这张页上按 Esc 也可以）"));
    m_back->setAccessibleName(QStringLiteral("返回商店"));
    m_back->setCursor(Qt::PointingHandCursor);
    m_back->setVisible(false);
    m_heading = new QLabel(QStringLiteral("插件商店"), this);
    m_heading->setObjectName(QStringLiteral("pageTitle"));
    auto* refresh = new QPushButton(QStringLiteral("刷新清单"), this);
    refresh->setObjectName(QStringLiteral("storeRefresh"));
    refresh->setCursor(Qt::PointingHandCursor);
    refresh->setToolTip(QStringLiteral("拉商店那份 plugins-index.json。\n"
                                      "内置地址是**和这版 ltool 同一次构建发出去的那一份**（GitHub Release 资产），"
                                      "地址在配置页「插件清单地址」那一行，代理沿用「下载代理」那一行。"));
    auto* repoBtn = new QPushButton(QStringLiteral("插件仓库"), this);
    repoBtn->setObjectName(QStringLiteral("storeRepo"));
    repoBtn->setCursor(Qt::PointingHandCursor);
    repoBtn->setToolTip(QStringLiteral("前往 ltool_plugin GitHub 插件仓库网页"));
    connect(repoBtn, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/mateo-ki/ltool_plugin")));
    });

    const char* const kTopBtnQss =
        "QPushButton {"
        "  background: @button;"
        "  border: 1px solid @borderHi;"
        "  color: @text;"
        "  border-radius: 7px;"
        "  font-size: 9pt;"
        "  font-weight: 500;"
        "  padding: 4px 12px;"
        "  min-height: 26px;"
        "}"
        "QPushButton:hover { background: @buttonUp; border-color: @borderHiUp; }"
        "QPushButton:pressed { background: @buttonDown; }"
    ;
    Theme::bind(refresh, kTopBtnQss);
    Theme::bind(repoBtn, kTopBtnQss);
    // 出口那颗和它们同款：autoRaise 的淡箭头在深色页眉里就是"找不到退出按钮"（他 2026-10-18 的原话）
    Theme::bind(m_back, kTopBtnQss);

    head->addWidget(m_back);
    head->addWidget(m_heading);
    head->addStretch(1);
    head->addWidget(repoBtn);
    head->addWidget(refresh);
    lay->addLayout(head);

    // 启动优先载入持久化清单缓存
    const QByteArray cached = PluginStore::loadCachedIndex();
    if (!cached.isEmpty()) {
        QStringList probs;
        const auto entries = PluginStore::parseIndex(cached, &probs);
        if (!entries.isEmpty()) {
            m_haveStore = true;
            m_lastEntries = entries;
            m_lastProblems = probs;
        }
    }

    m_storeStatus = new QLabel(this);
    m_storeStatus->setObjectName(QStringLiteral("tip"));
    m_storeStatus->setWordWrap(true);
    if (m_haveStore) {
        m_storeStatus->setText(QStringLiteral("已载入本地清单缓存（共 %1 条插件）· 每天自动检查更新一次").arg(m_lastEntries.size()));
    } else {
        m_storeStatus->setText(QStringLiteral(
            "这一面列的是本机 `ltool-plugins\\` 里装着的那几个。点右上「刷新清单」，仓库里能装的也进这同一面。"));
    }
    lay->addWidget(m_storeStatus);

    // 自动更新那一趟的结果：常驻一行，不挂在某次动作的状态上（他不一定在商店页时才跑完）。
    m_autoNote = new QLabel(this);
    m_autoNote->setObjectName(QStringLiteral("tip"));
    m_autoNote->setWordWrap(true);
    m_autoNote->setText(autoUpdateLine());
    lay->addWidget(m_autoNote);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(buildGrid());     // 0 = 那一面卡片，插件页往后追加
    lay->addWidget(m_stack, 1);

    connect(m_back, &QToolButton::clicked, this, [this] { backToGrid(); });
    connect(refresh, &QPushButton::clicked, this, [this] { refreshStore(); });

    // 每天自动拉取一次清单更新
    if (PluginStore::shouldAutoRefresh() || !m_haveStore) {
        QTimer::singleShot(100, this, [this] { refreshStore(); });
    }
}

QVector<PluginStore::Row> ToolsPage::shopRows() const
{
    // 一条列表、一条规则，绝不出现"墙是一批、清单是另一批"：
    //   · 没拉过清单 → 只报目录里有什么（不猜仓库里有什么）
    //   · 拉过 → 以清单为准，后面跟上本机装着而清单没列的那几个
    QVector<PluginStore::Row> out;
    if (!m_haveStore) {
        out = PluginStore::localRows(QString());
    } else {
        out = PluginStore::rows(m_lastEntries);
        const QVector<PluginStore::Row> extra = PluginStore::localOnly(m_lastEntries);
        for (const PluginStore::Row& r : extra)
            out.append(r);
    }
    // "排进待删表"这件事只有配置里那张表知道，所以在这里补到数据层上 ——
    // 卡片重排之后它照样说得出真话（写在卡片标签上活不过一次重排）。
    const QVector<QString> queued = PluginStore::pendingRemoves();
    if (!queued.isEmpty())
        for (PluginStore::Row& r : out)
            r.queuedRemove = queued.contains(r.remote.file);
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
    grid->setSpacing(12);
    for (int i = 0; i < rows.size(); ++i) {
        grid->addWidget(makeCard(rows[i]), i / kGridColumns, i % kGridColumns);
        grid->setColumnStretch(i % kGridColumns, 1);
    }
    lay->addLayout(grid);
    lay->addStretch(1);

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("toolsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(page);
    return scroll;
}

// "卸了之后还能不能装回来"只有拉过清单才答得上来；没拉过就别假装有把握
QString ToolsPage::cannotOpenText(const QString& stateWord, const QString& loadErr,
                                  const QString& metaErr)
{
    const QString why = !loadErr.isEmpty()    ? loadErr
                        : !metaErr.isEmpty()  ? metaErr
                                              : QStringLiteral("目录里认不出这个文件");
    return QStringLiteral("%1 · 本机有这个 dll 但它起不来：%2").arg(stateWord, why);
}

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
    auto* card = new ShopCard(r.remote.name, r.remote.desc, colorOf(r.remote.color, r.remote.id), r.remote.version, this);
    // 状态是数据层那个枚举，判据不该靠猜文案认它（"↑ 可更新"带个箭头前缀，startsWith 就认不出了，
    // 2026-10-17 那 4 条红就是这里来的）。文案想怎么排版都行，这一颗钉住"这张卡说的是什么状态"。
    card->setProperty("ltoolState", static_cast<int>(r.state));
    QString stateWord = PluginStore::stateWord(r.state);
    // 可更新 / 与商店不同 这两档各带着两个版本号（本机一个、商店一个）。
    // 往状态词后面拼一个孤零零的 (v1.0.0) 会让人以为"要更新到 1.0.0"，
    // 而那印的本来就是本机自己那份 —— 版本对照写在悬停提示里（note 那句）。
    const bool twoVersions = r.state == PluginStore::State::Update
                             || r.state == PluginStore::State::Different;
    if (!r.remote.version.isEmpty() && !twoVersions)
        stateWord = QStringLiteral("%1 (v%2)").arg(stateWord, r.remote.version);
    QString noteText;
    if (r.state == PluginStore::State::Installed) {
        const int idx = r.note.lastIndexOf(QLatin1String(" · "));
        noteText = (idx >= 0) ? r.note.mid(idx + 3) : QString();
    } else if (r.state == PluginStore::State::Missing) {
        const int idx = r.note.indexOf(QLatin1String("（清单 "));
        if (idx >= 0) {
            noteText = r.note.mid(idx + 5);
            if (noteText.endsWith(QLatin1Char(')')) || noteText.endsWith(QString::fromUtf8("）")))
                noteText.chop(1);
        } else {
            noteText = r.note;
        }
    } else if (r.state == PluginStore::State::Update || r.state == PluginStore::State::Different) {
        // 「↑ 可更新」那颗绿箭头本身就是答复，不需要再把"本机 xxxx… ≠ 清单 yyyy…"那串写在脸上。
        // 大小留着（他要看得见装的是多大一份），哈希对照挪进悬停。
        noteText = PluginStore::humanSize(r.remote.bytes);
        card->setToolTip(r.note);
    } else {
        noteText = r.note;
    }
    // 排进待删表的那张：整条状态换掉。文件确实还在盘上，可"已装"这句已经是假话 ——
    // 他 11:17 连点两次「卸载」，就是因为这张卡每次重排都重新说"已装"（写在标签上的那句
    // 活不过一次重排，所以这条改在数据层：见 PluginStore::Row::queuedRemove）。
    if (r.queuedRemove) {
        stateWord = QStringLiteral("已安排卸载");
        noteText = QStringLiteral("这个进程还加载着它 · 下次启动真删掉");
    }
    card->setUpdateArrow(r.state == PluginStore::State::Update && !r.queuedRemove);
    card->stateLabel()->setText(noteText.isEmpty()
                                    ? stateWord
                                    : QStringLiteral("%1 · %2").arg(stateWord, noteText));
    if (r.state == PluginStore::State::Update)
        Theme::bind(card->stateLabel(), "color: @ok; font-weight: 600;");

    using S = PluginStore::State;
    const PluginStore::Entry e = r.remote;
    const QString id = r.remote.id;
    const QString file = r.remote.file;
    // "能不能点开"看的不是状态词，是宿主认不认得出那个 dll：
    // 已装但没通过检查的那条也得老实承认打不开，不能给一只手形然后什么都发生不了。
    const PluginHost::Meta* md = PluginHost::meta(id);
    const bool openable = md && md->ok();
    const bool installed = r.state == S::Installed || r.state == S::Update
                         || r.state == S::LocalOnly || r.state == S::Different;
    // 这三颗是给离屏判据挂的"决策的输入"：卡片红不红要能说出是哪一半决定的，
    // 只量状态词和按钮的话，"正文给了空函数"这一类（10-20 他报的点不开）会漏掉。
    card->setProperty("ltoolId", id);
    card->setProperty("ltoolOpenable", openable);
    // 「清单这一条在这台机器上装得了吗」管的是「装 / 更新 / 换成商店那份」那三颗按钮，
    // 不许管正文点击、禁用、卸载 —— 那是上面那颗 openable 的事。
    card->setProperty("ltoolEntryUsable", r.entryUsable);

    switch (r.state) {
    case S::Missing:
    case S::Update: {
        // 商店那一条在这台机器上装得了吗（`Api` 对得上）。对不上就**不给「装/更新」**，
        // 但本机那颗照旧能禁、照旧能卸、照旧能点开 —— 10-20 他报"注册表/开机分析还是点不开"，
        // 形状就是原来那张卡被整条锁死：一颗按钮都不给、点正文也没反应。
        QPushButton* install = nullptr;
        if (r.entryUsable) {
            install = card->addButton(r.state == S::Missing ? QStringLiteral("装")
                                                            : QStringLiteral("更新"),
                                      /*isPrimary=*/true);
            connect(install, &QPushButton::clicked, this, [this, e, card, install] {
                install->setEnabled(false);
                installEntry(e, install, card->stateLabel());
            });
        }
        if (r.state == S::Update) {
            // 本机确实装着它（只是哈希不同）：禁得掉、也卸得掉，不能只剩"更新"这一条路
            addToggleButton(this, card, file, /*enableIt=*/false);
            addUninstallButton(this, card, e.name, file,
                                QStringLiteral("清单里有它，卸完还能从这一面再装回来。"));
        }
        if (r.state == S::Missing && install) {
            // 还没装的那张：点正文就是按同一颗按钮。这里绝不能去 showTool —— 本机没那个 dll
            // （而"没装着 + 商店那条对不上号"根本走不到这里：那一档在 compare 里就叫 ApiMismatch）
            card->setBodyClick([install] { install->click(); });
        } else if (r.state == S::Update) {
            // 「可更新」是装了但哈希不同：那张页本来就能看，正文照常打开，更新交给按钮
            card->setBodyClick(openable ? [this, id] { showTool(id); } : std::function<void()>());
        }
        break;
    }
    case S::Different: {
        // 本机装着、哈希与商店那份不一样，但商店版本号不比本机新 —— 这不叫"有更新"。
        // 出路还是要给：开发机上手工同步过、或本机这份是更旧的测试产物时，能换回商店那份。
        // 商店那条对不上号时这条出路是坏的（换过去当场起不来），所以它跟着 entryUsable 走。
        if (r.entryUsable) {
            auto* swap = card->addButton(QStringLiteral("换成商店那份"), /*isPrimary=*/true);
            connect(swap, &QPushButton::clicked, this, [this, e, card, swap] {
                swap->setEnabled(false);
                installEntry(e, swap, card->stateLabel());
            });
        }
        addToggleButton(this, card, file, /*enableIt=*/false);
        addUninstallButton(this, card, e.name, file, reinstallHint(r));
        card->setBodyClick(openable ? [this, id] { showTool(id); } : std::function<void()>());
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
    if (installed && !openable) {
        // 这一行原来有个洞：`md` 存在时三元取的是 `QString()` —— 界面上就是一句以冒号结尾的
        // "本机有这个 dll 但没通过检查："，后面什么都没有。他 2026-10-20 问"注册表分析和开机
        // 分析为什么点不开"，卡片上确实标了"没通过检查"，可**没说什么检查没过**。
        card->stateLabel()->setText(ToolsPage::cannotOpenText(
            stateWord, PluginHost::loadErrorOf(id), md ? md->error : QString()));
        Theme::bind(card->stateLabel(), "color: @bad; font-weight: 600;");
    }
    // 最后那颗要在所有 setBodyClick 都走完之后读：它量的是"这件事到底给没给"。
    // （第一版我把它写在 makeCard 开头，读到的永远是 false —— 19 张卡一起红，红的是判据自己。）
    card->setProperty("ltoolClickable", card->hasBodyClick());
    return card;
}

QString ToolsPage::autoUpdateLine()
{
    // 三种情况都得说得出话：关着、开着但今天还没跑、跑完了（成了几颗 / 哪几颗没成）。
    // 关着的时候不能留一行空白 —— 那看起来像"这个功能坏了"。
    if (!PluginStore::autoUpdateEnabled())
        return QStringLiteral("自动更新：没开（配置页「通用」那一节那颗开关）。这一面只在你要装的时候才动文件。");
    const QString note = PluginStore::autoUpdateNote();
    if (note.isEmpty())
        return QStringLiteral("自动更新：开着，今天这一趟还没跑（程序启动之后十几秒跑，一天一次，只换已装的那几颗）。");
    return QStringLiteral("自动更新：%1").arg(note);
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
        if (m_autoNote)
            m_autoNote->setText(autoUpdateLine());
        if (!got.ok) {
            if (!m_lastEntries.isEmpty()) {
                m_storeStatus->setText(QStringLiteral("网络拉取未成功：%1 —— 当前展示本地缓存清单（共 %2 条）")
                                           .arg(got.err).arg(m_lastEntries.size()));
                rebuildGrid();
                Utils::log(QStringLiteral("store: 拉清单失败 %1，使用本地缓存").arg(got.err));
                return;
            }
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
        PluginStore::saveCachedIndex(got.body);
        PluginStore::markFetched();

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

// 「装/更新」按下去先进这里：已经有一颗在飞就排队。
// 为什么非要排队 —— 他连点九颗那次，九条下载 + 九次改名 + 中途 reload/重排叠在一起，
// 最后崩在 scan() 里（Qt6Widgets，0xc0000005）。一次一颗把这堆叠态整个拿掉，
// 顺带也不会把镜像同时打九个洞。
void ToolsPage::installEntry(const PluginStore::Entry& e, QPushButton* btn, QLabel* state)
{
    if (m_installing > 0) {
        m_queue.append({ e, btn, state });
        if (btn)
            btn->setText(QStringLiteral("排队"));
        if (state)
            state->setText(QStringLiteral("前面还有 %1 颗，一颗落地就轮到它").arg(m_queue.size()));
        return;
    }
    runInstall(e, btn, state);
}

void ToolsPage::runInstall(const PluginStore::Entry& e, QPushButton* btn, QLabel* state)
{
    // 这两颗是**卡片上的控件**，而 rebuildGrid() 会把整格换掉（装完一颗、点一次「刷新清单」都会）。
    // 连的是 this（页面还活着），所以卡片被删之后回调照样会跑 —— 用裸指针就是往已删的 QWidget 上写：
    // 他"连着点多几颗装"闪退的就是这条（第一颗装完拆墙，第二颗的进度回调打在没了的按钮上）。
    QPointer<QPushButton> btnRef = btn;
    QPointer<QLabel> stateRef = state;
    ++m_installing;   // 有几颗在飞：最后一颗落地才重排卡片墙，见下面 done 里那句
    // 下载那一段在 `PluginStore::installEntry`（2026-10-21 搬出去的，自动更新那趟没有界面、
    // 要问同一件事）。这儿只剩"把进度写成字"这一件界面的事。
    PluginStore::installEntry(e, this,
                       [btnRef](qint64 got, qint64 all) {
                           // 就地改按钮上那行字，不新插一块进度条：版面一跳就是"突然出现在上面"
                           // 卡片已经被拆掉的话，这行字没地方写 —— 直接不写，下载照下。
                           if (btnRef)
                               btnRef->setText(all > 0 ? QStringLiteral("%1%").arg(got * 100 / all)
                                                       : PluginStore::humanSize(got));
                       },
                       [this, e, btnRef, stateRef](const PluginStore::InstallResult& r) {
                           if (btnRef)
                               btnRef->setEnabled(true);
                           if (!r.ok) {
                               if (stateRef)
                                   stateRef->setText(r.err);
                               if (btnRef)
                                   btnRef->setText(QStringLiteral("重试"));
                               Utils::log(QStringLiteral("store: 装 %1 没成：%2").arg(e.file, r.err));
                           } else {
                               Utils::log(QStringLiteral("store: 装好 %1（%2 拿到，哈希对过，%3 B）")
                                              .arg(e.file, r.via).arg(e.bytes));
                               if (stateRef)
                                   stateRef->setText(QStringLiteral("已装好 · 下次启动进卡片墙"));
                           }
                           if (--m_installing == 0)
                               startNextInstall();
                       });
}

// 队列空了才做那一次性的重扫 + 重排；还有下一颗就直接开下一条（控件此刻还都活着）。
void ToolsPage::startNextInstall()
{
    if (m_queue.isEmpty()) {
        finishInstalls();
        return;
    }
    const Queued q = m_queue.takeFirst();
    // 卡片可能已经被拆掉了（他中途点了「刷新清单」，或者上一颗落地时重排过）：
    // 那就没地方写字，但这一颗照样要装下去 —— 装不装是文件的事，写字才是界面的事。
    if (q.btn)
        q.btn->setText(QStringLiteral("下载中"));
    if (q.state)
        q.state->setText(QStringLiteral("轮到它了（后面还有 %1 颗）").arg(m_queue.size()));
    runInstall(q.entry, q.btn.data(), q.state.data());
}

// 一批安装全落地之后只做一次：重扫插件目录 + 重排卡片墙。
// 一颗装完就 rebuildGrid() 是那次闪退的另一半 —— 它把还在下载的那几张卡的控件删了。
void ToolsPage::finishInstalls()
{
    PluginHost::reload();
    ShellMenu::syncWithPlugin();     // 把「右键动作」装回来 = 右键里那几项也该跟着回来
    rebuildGrid();
}

bool ToolsPage::showTool(const QString& id, const QString& text)
{
    QString err;
    QWidget* w = PluginHost::page(id, this, &err);
    if (!w) {
        // 光写日志等于没写：他点了卡片，屏幕上只是回到卡片墙，谁也看不出"点了但没开成"。
        // 页眉就在卡片墙顶上，那一行本来就是"现在在哪个插件"的位置 —— 把原因写在那儿。
        const PluginHost::Meta* md = PluginHost::meta(id);
        m_heading->setText(QStringLiteral("插件商店 · %1 打不开：%2")
                               .arg(md ? md->name : id,
                                    err.isEmpty() ? QStringLiteral("原因未知（看日志 plugin: 那几行）")
                                                  : err));
        Utils::log(QStringLiteral("plugin: 打不开 %1：%2").arg(id, err));
        return false;
    }
    if (m_stack->indexOf(w) < 0)
        m_stack->addWidget(w);
    if (!text.isEmpty() && !PluginHost::feed(id, text))
        Utils::log(QStringLiteral("plugin: %1 没接住那段文本（长度 %2）").arg(id).arg(text.size()));
    const int was = m_stack->currentIndex();
    m_stack->setCurrentWidget(w);
    // 他这次报的是"商店自己跳到了那张页"，可三条路（点卡片正文 / 搜索框直达 / 设置窗 openAt）
    // 在日志里长得一模一样，查不出来。这条把"从第几格跳到第几格"写下来，下次再报就有据。
    Utils::log(QStringLiteral("store: 展开 %1（stack 第 %2 格 / 共 %3 格，之前在第 %4 格）")
                   .arg(id)
                   .arg(m_stack->indexOf(w))
                   .arg(m_stack->count())
                   .arg(was));
    m_back->setVisible(true);
    const PluginHost::Meta* md = PluginHost::meta(id);
    m_heading->setText(QStringLiteral("插件商店 · %1").arg(md ? md->name : id));
    return true;
}

void ToolsPage::rebuildGrid()
{
    // 只换卡片那一格，已经点开过的插件页留在 stack 里不动（dll 不卸载，拆了也没意义）。
    //
    // 顺序是**先插新的、再摘旧的**。反过来写（先摘后插）会把上一张点开的插件页闪出来一次：
    // QStackedLayout 在"当前那一格被摘掉"的瞬间会把后面的格子顶成当前，于是那张页收到一次
    // Show —— 屏幕上就是"卸完插件，商店自己跳到了注册表分析那一页"（2026-10-18 他报的形状，
    // --storeexit 第⑥条量的就是这件事：旧顺序 Show 1 次，这个顺序 0 次）。
    QWidget* old = m_stack->widget(0);
    QWidget* grid = buildGrid();
    m_stack->insertWidget(0, grid);
    m_stack->setCurrentWidget(grid);
    if (old) {
        m_stack->removeWidget(old);
        old->deleteLater();
    }
    backToGrid();
    // 他报的是"卸完插件……然后内存爆炸卡死"，而那台是 Win10（我这台 Win11 上离屏量到 58→63 MB，
    // 复现不了）。每重排一次就把工作集和格子数写进日志：下次炸之前那几行能分出
    // "每次重排都长一点"（= 有东西没释放）和"某一下突然长"（= 那一下真去干了重活）。
    Utils::log(QStringLiteral("store: 重排卡片墙（卡片 %1 张 / stack %2 格 / 工作集 %3 MB）")
                   .arg(findChildren<QFrame*>(QStringLiteral("row")).size())
                   .arg(m_stack->count())
                   .arg(Platform::workingSetMB()));
}

// 回到那一面卡片墙：三处入口（那颗按钮、Esc、重排之后）共用这一条
void ToolsPage::backToGrid()
{
    m_stack->setCurrentIndex(0);
    m_back->setVisible(false);
    m_heading->setText(QStringLiteral("插件商店"));
}

void ToolsPage::keyPressEvent(QKeyEvent* e)
{
    // 那张页的第二个出口。他原话是"找不到退出按钮" —— 按钮再实心也还是页眉里小小一颗，
    // 所以 Esc 也得能回去。**必须把这个事件吃掉**：设置窗是个 QDialog，Esc 传上去会把整窗关掉。
    if (e->key() == Qt::Key_Escape && m_stack && m_stack->currentIndex() != 0) {
        backToGrid();
        e->accept();
        return;
    }
    QWidget::keyPressEvent(e);
}
