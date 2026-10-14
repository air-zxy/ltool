#include "ToolsPage.h"

#include "DevEnv.h"
#include "PluginHost.h"
#include "PluginStore.h"
#include "Utils.h"

#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMouseEvent>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <functional>

namespace {

// 一张可点击的小卡片：整块 hover 有反馈，点下去执行绑定的动作
class ToolCard : public QFrame {
public:
    ToolCard(const QString& title, const QString& desc, const QColor& color,
             std::function<void()> onClick, QWidget* parent)
        : QFrame(parent), m_onClick(std::move(onClick))
    {
        setObjectName(QStringLiteral("row"));
        setAttribute(Qt::WA_StyledBackground);
        setCursor(Qt::PointingHandCursor);
        setMinimumHeight(86);
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
        auto* info = new QLabel(desc, this);
        info->setObjectName(QStringLiteral("muted"));
        info->setWordWrap(true);
        text->addWidget(name);
        text->addWidget(info);
        lay->addWidget(icon, 0, Qt::AlignTop);
        lay->addLayout(text, 1);
    }

protected:
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && rect().contains(e->pos()) && m_onClick)
            m_onClick();
        QFrame::mouseReleaseEvent(e);
    }

private:
    std::function<void()> m_onClick;
};

constexpr int kGridColumns = 3;

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
    m_back->setText(QStringLiteral("‹ 返回工具"));
    m_back->setAutoRaise(true);
    m_back->setCursor(Qt::PointingHandCursor);
    m_back->setVisible(false);
    m_heading = new QLabel(QStringLiteral("工具"), this);
    m_heading->setObjectName(QStringLiteral("pageTitle"));
    head->addWidget(m_back);
    head->addWidget(m_heading);
    head->addStretch(1);
    lay->addLayout(head);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(buildGrid());     // 0 = 卡片墙，插件页往后追加
    lay->addWidget(m_stack, 1);

    connect(m_back, &QToolButton::clicked, this, [this] {
        m_stack->setCurrentIndex(0);
        m_back->setVisible(false);
        m_heading->setText(QStringLiteral("工具"));
    });
}

QWidget* ToolsPage::buildGrid()
{
    auto* page = new QWidget(this);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(8);

    const auto& metas = PluginHost::scan();
    int good = 0;
    QStringList bad;
    for (const PluginHost::Meta& m : metas) {
        if (m.ok())
            ++good;
        else
            bad << QStringLiteral("%1：%2").arg(QFileInfo(m.file).fileName(), m.error);
    }

    QString tip = QStringLiteral("点一张卡片进去。这些工具是 `<exe 目录>\\ltool-plugins\\` 里的 dll，"
                                 "卡片只读元数据，点开才真加载；在搜索框里直接搜名字也能打开。");
    if (good == 0)
        tip = bad.isEmpty()
                  ? QStringLiteral("工具目录 %1 里一个插件都没有 —— 这个安装是坏的（重装一次），"
                                    "或者你把那些 dll 挪走了。")
                        .arg(PluginHost::pluginsDir())
                  : QStringLiteral("工具目录 %1 里没有能用的插件：%2")
                        .arg(PluginHost::pluginsDir(), bad.join(QStringLiteral("；")));
    else if (!bad.isEmpty())
        tip = QStringLiteral("%1 · 另有 %2 个文件没通过检查：%3")
                  .arg(tip).arg(bad.size()).arg(bad.join(QStringLiteral("；")));

    auto* hint = new QLabel(tip, page);
    hint->setObjectName(QStringLiteral("tip"));
    hint->setWordWrap(true);
    lay->addWidget(hint);

    auto* grid = new QGridLayout;
    grid->setSpacing(8);
    int slot = 0;
    for (const PluginHost::Meta& m : metas) {
        if (!m.ok())
            continue;
        const QString id = m.id;
        auto* card = new ToolCard(m.name, m.desc, m.color, [this, id] { showTool(id); }, page);
        grid->addWidget(card, slot / kGridColumns, slot % kGridColumns);
        grid->setColumnStretch(slot % kGridColumns, 1);
        ++slot;
    }
    lay->addLayout(grid);
    lay->addWidget(buildStore());
    lay->addStretch(1);
    return page;
}

void ToolsPage::rebuildGrid()
{
    // 只换卡片墙那一格，已经点开过的插件页留在 stack 里不动（dll 不卸载，拆了也没意义）
    QWidget* old = m_stack->widget(0);
    m_stack->removeWidget(old);
    old->deleteLater();
    m_stack->insertWidget(0, buildGrid());
    m_back->setVisible(false);
    m_heading->setText(QStringLiteral("工具"));
}

QWidget* ToolsPage::buildStore()
{
    auto* box = new QFrame(this);
    // 不是 "row"：卡片墙那面判据就是数 objectName=="row" 的框（插件数 = 卡片数），
    // 商店这一整块借了同一个样式名会被数成第十三张卡。样式在 SettingsDialog 的表里另列一条。
    box->setObjectName(QStringLiteral("storeBox"));
    box->setAttribute(Qt::WA_StyledBackground);
    auto* lay = new QVBoxLayout(box);
    lay->setContentsMargins(12, 10, 12, 10);
    lay->setSpacing(6);

    auto* head = new QHBoxLayout;
    head->setSpacing(8);
    auto* title = new QLabel(QStringLiteral("插件商店"), box);
    QFont tf = title->font();
    tf.setBold(true);
    title->setFont(tf);
    m_storeStatus = new QLabel(QStringLiteral("还没拉过清单 —— 点「刷新清单」（走的地址在配置页那一行）。"), box);
    m_storeStatus->setObjectName(QStringLiteral("muted"));
    m_storeStatus->setWordWrap(true);
    auto* refresh = new QPushButton(QStringLiteral("刷新清单"), box);
    refresh->setObjectName(QStringLiteral("storeRefresh"));
    refresh->setCursor(Qt::PointingHandCursor);
    head->addWidget(title);
    head->addStretch(1);
    head->addWidget(refresh);
    lay->addLayout(head);
    lay->addWidget(m_storeStatus);

    m_storeRows = new QVBoxLayout;
    m_storeRows->setSpacing(4);
    lay->addLayout(m_storeRows);

    connect(refresh, &QPushButton::clicked, this, [this] { refreshStore(); });
    if (m_haveStore) {
        // 拿缓存的**清单**重新比一次本地目录，不是把上一屏那几个字抄回来：
        // 刚装完/刚禁用完走到这一步，状态得跟着目录里的文件动。
        m_storeStatus->setText(m_lastStatus);
        showStoreRows(PluginStore::rows(m_lastEntries), m_lastProblems);
    }
    return box;
}

void ToolsPage::showStoreRows(const QVector<PluginStore::Row>& rows, const QStringList& problems)
{
    // 换行之前把旧的拆干净：这一栏就地改字，不做"新插一块到上面"那种动作
    while (QLayoutItem* it = m_storeRows->takeAt(0)) {
        if (QWidget* w = it->widget())
            w->deleteLater();
        delete it;
    }
    for (const PluginStore::Row& r : rows) {
        auto* row = new QFrame(this);
        row->setObjectName(QStringLiteral("storeRow"));   // 同上：别和卡片同名，判据数的是卡片
        auto* lay = new QHBoxLayout(row);
        lay->setContentsMargins(2, 2, 2, 2);
        lay->setSpacing(8);

        auto* name = new QLabel(r.remote.name, row);
        name->setMinimumWidth(96);
        auto* state = new QLabel(PluginStore::stateWord(r.state), row);
        state->setMinimumWidth(64);
        auto* note = new QLabel(r.note, row);
        note->setObjectName(QStringLiteral("muted"));
        note->setWordWrap(true);
        lay->addWidget(name);
        lay->addWidget(state);
        lay->addWidget(note, 1);

        using S = PluginStore::State;
        const QString file = r.remote.file;
        if (r.state == S::Missing || r.state == S::Update) {
            const PluginStore::Entry e = r.remote;
            auto* btn = new QPushButton(r.state == S::Missing ? QStringLiteral("装") : QStringLiteral("更新"), row);
            btn->setCursor(Qt::PointingHandCursor);
            connect(btn, &QPushButton::clicked, this, [this, e, btn, state] {
                state->setText(QStringLiteral("正在下…"));
                btn->setEnabled(false);
                installEntry(e, btn, state);
            });
            lay->addWidget(btn);
        } else if (r.state == S::Installed || r.state == S::Disabled) {
            const bool on = r.state == S::Installed;
            auto* btn = new QPushButton(on ? QStringLiteral("禁用") : QStringLiteral("启用"), row);
            btn->setCursor(Qt::PointingHandCursor);
            connect(btn, &QPushButton::clicked, this, [this, file, on, state, note] {
                const QString err = PluginStore::setEnabled(file, !on);
                state->setText(err.isEmpty()
                                   ? QStringLiteral("已%1 · 下次启动生效").arg(on ? QStringLiteral("恢复") : QStringLiteral("禁用"))
                                   : err);
                note->setText(err.isEmpty() ? note->text() : err);
                Utils::log(QStringLiteral("store: %1 %2%3")
                               .arg(on ? QStringLiteral("恢复") : QStringLiteral("禁用"), file,
                                  err.isEmpty() ? QString() : QStringLiteral(" 失败：") + err));
                if (err.isEmpty())
                    rebuildGrid();
            });
            lay->addWidget(btn);
        }
        m_storeRows->addWidget(row);
    }
    if (!problems.isEmpty()) {
        auto* bad = new QLabel(QStringLiteral("清单里有 %1 条没收下：%2")
                                   .arg(problems.size()).arg(problems.join(QStringLiteral("；"))), this);
        bad->setObjectName(QStringLiteral("muted"));
        bad->setWordWrap(true);
        m_storeRows->addWidget(bad);
    }
}

void ToolsPage::refreshStore()
{
    if (m_storeBusy)
        return;
    m_storeBusy = true;
    const QString url = PluginStore::indexUrl();
    m_storeStatus->setText(QStringLiteral("正在拉清单：%1").arg(url));

    // 管理器挂在页上：页（连带设置窗）销毁时 reply 一起断，回调里再用 this 已经被 Qt 摘掉
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
            m_storeStatus->setText(QStringLiteral("清单没拉到：%1%2 —— 本地这些插件照常能用")
                                       .arg(reply->errorString(),
                                            http ? QStringLiteral("（HTTP %1）").arg(http) : QString()));
            m_haveStore = true;
            m_lastStatus = m_storeStatus->text();
            m_lastEntries.clear();
            m_lastProblems.clear();
            Utils::log(QStringLiteral("store: 拉清单失败 http=%1 err=%2 url=%3")
                           .arg(http).arg(reply->errorString(), url));
            return;
        }
        QStringList problems;
        const auto entries = PluginStore::parseIndex(body, &problems);
        const auto rs = PluginStore::rows(entries);
        int canInstall = 0;
        for (const PluginStore::Row& r : rs) {
            if (r.state == PluginStore::State::Missing || r.state == PluginStore::State::Update)
                ++canInstall;
        }
        m_storeStatus->setText(QStringLiteral("清单 %1 条（地址 %2）· 能装或可更新 %3 · 拒收 %4 —— "
                                              "装好的下次启动才进卡片墙，禁用的也是")
                                   .arg(entries.size()).arg(url).arg(canInstall).arg(problems.size()));
        showStoreRows(rs, problems);
        m_haveStore = true;
        m_lastStatus = m_storeStatus->text();
        m_lastEntries = entries;
        m_lastProblems = problems;
        Utils::log(QStringLiteral("store: 清单 %1 条，能装 %2，拒收 %3")
                       .arg(entries.size()).arg(canInstall).arg(problems.size()));
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
    connect(reply, &QNetworkReply::downloadProgress, this, [btn, e](qint64 got, qint64 all) {
        // 就地改按钮上那行字，不新插一块进度条：版面一跳就是"突然出现在上面"
        btn->setText(all > 0 ? QStringLiteral("%1%").arg(got * 100 / all)
                             : QStringLiteral("%1 KB").arg(got / 1024));
    });
    // readyRead 而不是 finished 之后一次 readAll：dll 上百 KB 到几 MB，一次读进来白占内存
    connect(reply, &QNetworkReply::readyRead, this, [reply, out] { out->write(reply->readAll()); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, out, e, btn, state, part, dir] {
        out->close();
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString netErr = reply->error() != QNetworkReply::NoError ? reply->errorString() : QString();
        reply->deleteLater();
        nam->deleteLater();
        out->deleteLater();
        const QString what = QStringLiteral("%1（%2 B）").arg(e.name).arg(e.bytes);
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
        state->setText(QStringLiteral("已装好 · 下次启动进卡片墙"));
        btn->setText(QStringLiteral("装好了"));
        Utils::log(QStringLiteral("store: 装好 %1（哈希对过，%2 B）").arg(e.file).arg(e.bytes));
        PluginHost::reload();
        rebuildGrid();
    });
}

bool ToolsPage::showTool(const QString& id, const QString& text)
{
    QString err;
    QWidget* w = PluginHost::page(id, this, &err);
    if (!w) {
        m_heading->setText(QStringLiteral("工具"));
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
    m_heading->setText(QStringLiteral("工具 · %1").arg(md ? md->name : id));
    return true;
}
