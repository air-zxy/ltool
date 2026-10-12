#include "ToolsPage.h"

#include "PluginHost.h"
#include "Utils.h"

#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

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
