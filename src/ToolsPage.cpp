#include "ToolsPage.h"

#include "AdsPage.h"
#include "Base64Tool.h"
#include "DiskPage.h"
#include "JsonTool.h"
#include "NetworkPage.h"
#include "RegistryPage.h"
#include "PluginHost.h"
#include "UserAccount.h"
#include "StartupPage.h"
#include "RepairPage.h"
#include "Utils.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

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

QVector<ToolDef> makeTools()
{
    QVector<ToolDef> v;    v.push_back({QStringLiteral("DLL 修复"),
                 QStringLiteral("检测微软运行库 / DirectX / .NET，缺什么补什么"),
                 {QStringLiteral("dll"), QStringLiteral("运行库"), QStringLiteral("vc++"),
                  QStringLiteral("vcruntime"), QStringLiteral("修复"), QStringLiteral("directx"),
                  QStringLiteral("dotnet"), QStringLiteral("net")},
                 QColor(214, 69, 69), 0});
    v.push_back({QStringLiteral("广告弹窗"),
                 QStringLiteral("扫描并拦截桌面广告弹窗，白名单与拦截规则"),
                 {QStringLiteral("广告"), QStringLiteral("弹窗"), QStringLiteral("ad"),
                  QStringLiteral("拦截"), QStringLiteral("广告弹窗")},
                 QColor(136, 87, 229), 1});
    v.push_back({QStringLiteral("磁盘清理"),
                 QStringLiteral("可清理位置 + 所选盘的占用分析（矩形树图）"),
                 {QStringLiteral("磁盘"), QStringLiteral("清理"), QStringLiteral("占用"),
                  QStringLiteral("空间"), QStringLiteral("c盘"), QStringLiteral("disk")},
                 QColor(87, 160, 40), 2});
    v.push_back({QStringLiteral("网络"),
                 QStringLiteral("系统代理检测、代理方案对比与内置下载设置"),
                 {QStringLiteral("网络"), QStringLiteral("代理"), QStringLiteral("proxy")},
                 QColor(0, 150, 199), 3});
    v.push_back({QStringLiteral("注册表分析"),
                 QStringLiteral("扫失效路径与 9x 遗留键：卸载残留 / 启动项 / 关联 / COM / 服务 / 死盘符"),
                 {QStringLiteral("注册表"), QStringLiteral("registry"), QStringLiteral("reg"),
                  QStringLiteral("残留"), QStringLiteral("废弃"), QStringLiteral("失效")},
                 QColor(150, 100, 60), 4});
    v.push_back({QStringLiteral("开机启动分析"),
                 QStringLiteral("开机后都有什么自己起来 · 谁家的 · 哪些该管哪些不用管"),
                 {QStringLiteral("启动"), QStringLiteral("开机"), QStringLiteral("自启"),
                  QStringLiteral("startup"), QStringLiteral("autostart"), QStringLiteral("服务"),
                  QStringLiteral("计划任务")},
                 QColor(60, 140, 120), 5});
    v.push_back({QStringLiteral("Windows 用户名"),
                 QStringLiteral("看清自己是哪个账户，改显示名或改账户名（都要管理员授权）"),
                 {QStringLiteral("用户名"), QStringLiteral("账户"), QStringLiteral("改名"),
                  QStringLiteral("user"), QStringLiteral("username"), QStringLiteral("account"),
                  QStringLiteral("fullname")},
                 QColor(120, 90, 200), 6});
    v.push_back({QStringLiteral("JSON 格式化"),
                 QStringLiteral("可折叠树 + 自动换行，文本区可直接改"),
                 {QStringLiteral("json"), QStringLiteral("格式化"), QStringLiteral("json格式化")},
                 QColor(90, 140, 220), -1});
    v.push_back({QStringLiteral("Base64 编解码"),
                 QStringLiteral("文本/图片互转，解不出合法 UTF-8 时给十六进制"),
                 {QStringLiteral("base64"), QStringLiteral("b64"), QStringLiteral("编码"),
                  QStringLiteral("解码")},
                 QColor(190, 130, 60), -2});
    return v;
}

constexpr int kGridColumns = 3;

} // namespace

const QVector<ToolDef>& ToolsPage::tools()
{
    static const QVector<ToolDef> v = makeTools();
    return v;
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
    m_stack->addWidget(buildGrid());     // 0 = 卡片墙
    for (int i = 0; i < 7; ++i) {
        auto* placeholder = new QWidget(m_stack);
        m_stack->addWidget(placeholder); // 1..4 = 内嵌页，首次点开才换真页面
    }
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

    auto* tip = new QLabel(QStringLiteral("点一张卡片进去。前六张在原位置展开，后两张是独立窗口；"
                                          "这些工具在搜索框里直接搜名字也能打开。"), page);
    tip->setObjectName(QStringLiteral("tip"));
    tip->setWordWrap(true);
    lay->addWidget(tip);

    auto* grid = new QGridLayout;
    grid->setSpacing(8);
    const auto& list = tools();
    for (int i = 0; i < list.size(); ++i)
        addCard(grid, i, list[i]);
    // DLL 插件排在自带卡片后面：只读元数据就能画卡，点开才真的 LoadLibrary
    int slot = list.size();
    for (const PluginHost::Meta& m : PluginHost::scan()) {
        if (!m.ok())
            continue;
        const QString id = m.id;
        auto* card = new ToolCard(m.name, m.desc, QColor(150, 120, 60),
                                [this, id] { showPlugin(id); }, this);
        grid->addWidget(card, slot / kGridColumns, slot % kGridColumns);
        grid->setColumnStretch(slot % kGridColumns, 1);
        ++slot;
    }
    lay->addLayout(grid);
    lay->addStretch(1);
    return page;
}

void ToolsPage::addCard(QGridLayout* grid, int slot, const ToolDef& td)
{
    auto* card = new ToolCard(td.name, td.desc, td.color, [this, slot] {
        const ToolDef& t = tools()[slot];
        if (t.embedded >= 0)
            showEmbedded(t.embedded);
        else if (t.embedded == -1)
            JsonTool::open();
        else
            Base64Tool::open();
    }, this);
    grid->addWidget(card, slot / kGridColumns, slot % kGridColumns);
    grid->setColumnStretch(slot % kGridColumns, 1);
}

void ToolsPage::showPlugin(const QString& id)
{
    QString err;
    QWidget* w = PluginHost::page(id, this, &err);
    if (!w) {
        m_heading->setText(QStringLiteral("插件"));
        Utils::log(QStringLiteral("plugin: 打不开 %1：%2").arg(id, err));
        return;
    }
    if (m_stack->indexOf(w) < 0)
        m_stack->addWidget(w);
    m_stack->setCurrentWidget(w);
    m_back->setVisible(true);
    m_heading->setText(QStringLiteral("插件 · %1").arg(w->objectName().isEmpty() ? id : w->objectName()));
}

void ToolsPage::showEmbedded(int which)
{
    QString title = QStringLiteral("工具");
    for (const ToolDef& t : tools())
        if (t.embedded == which)
            title = t.name;
    QWidget*& slot = m_embedded[which];
    if (!slot) {
        switch (which) {
        case 0: slot = new RepairPage(this); break;
        case 1: slot = new AdsPage(this); break;
        case 2: slot = new DiskPage(this); break;
        case 3: slot = new NetworkPage(this); break;
        case 4: slot = new RegistryPage(this); break;
        case 5: slot = new StartupPage(this); break;
        default: slot = new UserAccountPage(this); break;
        }
        m_stack->removeWidget(m_stack->widget(which + 1));
        m_stack->insertWidget(which + 1, slot);
    }
    m_stack->setCurrentIndex(which + 1);
    m_back->setVisible(true);
    m_heading->setText(title);
}
