#include "SettingsDialog.h"

#include "ConfigPage.h"
#include "SilentInstallLog.h"
#include "AboutPage.h"
#include "ToolsPage.h"
#include "WebsitesPage.h"
#include "Theme.h"
#include "Utils.h"

#include <QEvent>

#include <QHBoxLayout>
#include <QIcon>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <windows.h>
#include <dwmapi.h>

namespace {

// 加载标志的动画帧：一个缺了一口的圆环，按帧转。自绘，不依赖字体里的符号
QIcon spinnerIcon(int frame)
{
    QPixmap pm(14, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(QColor(120, 200, 255), 2, Qt::SolidLine, Qt::FlatCap);
    p.setPen(pen);
    p.drawArc(QRectF(1.5, 1.5, 11, 11), (360 - frame * 45) * 16, 110 * 16);
    p.end();
    return QIcon(pm);
}

// 设置页整体深色主题（子页面共享）
const char* kSettingsQss = R"QSS(
QDialog { background: @window; }

QLabel { color: @text; background: transparent; }
QLabel#tip { color: @muted; }
QLabel#pageTitle { font-size: 14pt; font-weight: 600; }
QLabel#muted { color: @muted; }

QGroupBox {
    color: @groupText;
    border: 1px solid @line;
    border-radius: 10px;
    margin-top: 16px;
    padding: 10px 6px 6px 6px;
    font-weight: 600;
    background: @group;
}
QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; }

QPlainTextEdit {
    background: @edit;
    border: 1px solid @line;
    border-radius: 8px;
    color: @okText;
    font-family: Consolas, monospace;
    font-size: 9pt;
}
QLineEdit, QComboBox, QSpinBox, QTimeEdit, QKeySequenceEdit {
    background: @field;
    border: 1px solid @border;
    border-radius: 8px;
    padding: 5px 8px;
    color: @text;
    selection-background-color: @accent;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QTimeEdit:focus, QKeySequenceEdit:focus {
    border: 1px solid @accent;
}
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView {
    background: @field;
    color: @text;
    selection-background-color: @accent;
    border: 1px solid @border;
    outline: none;
}

QPushButton {
    background: @button;
    color: @text;
    border: 1px solid @border;
    border-radius: 8px;
    padding: 6px 14px;
}
QPushButton:hover { background: @hover; }
QPushButton:pressed { background: @pressed; }
QPushButton:disabled { color: @disabled; background: @field; }
QPushButton#primary {
    background: @accent;
    border: 1px solid @accent;
    color: @onAccent;
    font-weight: 600;
}
QPushButton#primary:hover { background: @accentUp; }
QPushButton#primary:pressed { background: @accentDown; }
/* #primary 的 ID 选择器比 :disabled 优先，不补这条的话"禁用的主按钮"看着还是能点 */
QPushButton#primary:disabled { background: @field; border: 1px solid @line; color: @disabled; }
/* 列表行上的小动作按钮：一行四个，要紧凑，但禁用时必须一眼看出来 */
QPushButton#rowBtn {
    padding: 2px 9px;
    font-size: 8.5pt;
    border-radius: 6px;
}
QPushButton#rowBtn:disabled { background: transparent; border: 1px solid @line; color: @disabledLine; }
/* 软件列表的表头：比正文淡、比说明文字重一点，好认但不抢 */
QLabel#colHead { color: @muted; font-size: 8.5pt; font-weight: 600; background: transparent; }

QCheckBox { color: @text; spacing: 8px; background: transparent; }
QCheckBox::indicator {
    width: 16px; height: 16px;
    border: 1px solid @border;
    border-radius: 4px;
    background: @field;
}
QCheckBox::indicator:checked { background: @accent; border-color: @accent; }
QCheckBox::indicator:hover { border-color: @accentUp; }

QListWidget {
    background: @group;
    border: 1px solid @line;
    border-radius: 10px;
    color: @text;
    outline: none;
}
/* 条目背景不能只靠 padding：Qt 的行高来自 sizeHint，padding 只会把画出来的
   背景块撑得比行还高，于是上下条目的背景互相压住（语言列表行距 29、背景 47，
   压掉 18px，看着就是"上下背景色挤在一起"）。行高在下面用 setSizeHint 给足，
   这里只留水平方向的呼吸和圆角。 */
QListWidget::item { padding: 0 8px; border-radius: 6px; }
QListWidget::item:selected { background: @accent; color: @onAccent; }
QListWidget::item:hover:!selected { background: @hover; }

QListWidget#menuList {
    font-size: 11pt;
    font-weight: 500;
}
QListWidget#menuList::item {
    padding: 0 10px;
    border-radius: 8px;
}





QTabWidget::pane { border: none; background: transparent; }
QTabBar::tab { background: @field; color: @groupText; border: 1px solid @line;
    padding: 7px 16px; margin-right: 4px; border-top-left-radius: 8px; border-top-right-radius: 8px; }
QTabBar::tab:selected { background: @button; color: @text; border-bottom-color: @accent; }
QTabBar::tab:hover:!selected { background: @pressed; color: @text; }

QScrollArea#cfgScroll, QScrollArea#cfgScroll > QWidget#qt_scrollarea_viewport { background: transparent; }
QWidget#cfgBody { background: transparent; }
QWidget#rowsHost { background: transparent; }
/* 列表分组表头（软件页）：不属于 #row 的盒子，只要一行小字 + 上方的呼吸 */
QLabel#catHead { color: @muted; font-weight: 600; font-size: 9pt; padding: 6px 2px 0 2px; }
QWidget#row {
    background: @group;
    border: 1px solid @line;
    border-radius: 8px;
}
QWidget#row:hover { background: @hover; border-color: @border; }
QWidget#row QLabel { background: transparent; }

QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }
QScrollBar::handle:vertical { background: @border; border-radius: 4px; min-height: 24px; }
QScrollBar::handle:vertical:hover { background: @disabledLine; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
)QSS";

} // namespace

// 这一页的样式表 = 那份 token 化的 QSS + 弹层那一份。抽成一颗函数是因为
// Theme::setMode() 会发 QEvent::ThemeChange 过来，届时原样再算一遍（见 event()）。
void SettingsDialog::applyThemeQss()
{
    // QMenu 那几条不在这里写了：全程序只有 Theme::menuStyle() 那一份（Utils::darkMenuStyle
    // 现在是它的转接，插件那边符号名不能动）
    setStyleSheet(Theme::expand(kSettingsQss) + Theme::menuStyle());
}

bool SettingsDialog::event(QEvent* e)
{
    if (e->type() == QEvent::ThemeChange)
        applyThemeQss();     // 配置页拨完那颗单选，这扇已经开着的窗当场就要变脸
    return QDialog::event(e);
}

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("ltool 设置");
    resize(1140, 790);
    applyThemeQss();   // 见下面那颗：夜间/日间切换时要能原地重算这一份

    m_menu = new QListWidget(this);
    m_menu->setObjectName("menuList");
    m_menu->setFixedWidth(168);
    m_menu->setIconSize(QSize(28, 28));
    m_menu->setSpacing(5);              // 条目之间留缝，背景块不再首尾相接
    m_menu->setUniformItemSizes(true);

    struct MenuDef { const char* name; QColor color; bool bag; };
    // 只有四行。语言并进「配置」那一页里的一节，软件 / 下载器 / 定时任务搬成了插件商店里的卡片 ——
    // 颜色沿用它们原来那一行的，一眼还认得出来。
    // 「插件商店」那一行画的是一颗袋子（其余三行是第一个字）：四个图标里它是唯一"能买东西"的那个。
    const MenuDef defs[] = {
        {"网站", QColor(6, 182, 212), false},    // 活力青碧 (Cyan)：象征网络与探索
        {"插件商店", QColor(249, 115, 22), true}, // 暖阳珊瑚橙 (Orange)：白色百宝袋在暖橙底上更鲜明
        {"配置", QColor(59, 130, 246), false},   // 经典科技蓝 (Blue)：沉稳、专业的首选项控制
        {"关于", QColor(168, 85, 247), false},   // 极光罗兰紫 (Purple)：高雅、醒目的版本与品牌
    };
    for (const MenuDef& d : defs) {
        const QIcon icon = d.bag ? Utils::bagIcon(d.color) : Utils::letterIcon(d.name, d.color);
        auto* it = new QListWidgetItem(icon, d.name);
        it->setSizeHint(QSize(168, 50));   // 行高由这里定，QSS 的 padding 不再撑破邻居
        m_menu->addItem(it);
    }

    m_stack = new QStackedWidget(this);
    for (int i = 0; i < 4; ++i)
        m_stack->addWidget(new QWidget(m_stack)); // 占位，选中时懒加载

    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(10, 10, 10, 10);
    lay->setSpacing(12);
    auto* menuCol = new QVBoxLayout;
    menuCol->setContentsMargins(0, 0, 0, 0);
    menuCol->setSpacing(6);
    menuCol->addWidget(m_menu, 1);
    buildBusyRow();
    menuCol->addWidget(m_busy);
    lay->addLayout(menuCol);
    lay->addWidget(m_stack, 1);

    connect(m_menu, &QListWidget::currentRowChanged, this, &SettingsDialog::ensurePage);
    m_menu->setCurrentRow(0);

}

// 菜单左下角的加载标志：后台有静默安装在跑才显示，点开「静默安装日志」看台（在那里取消）。
// 装完自己消失，不用用户去猜现在到底有没有在跑。
void SettingsDialog::buildBusyRow()
{
    m_busy = new QToolButton(this);
    m_busy->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_busy->setAutoRaise(true);
    m_busy->setCursor(Qt::PointingHandCursor);
    m_busy->setFixedHeight(28);
    Theme::bind(m_busy,
        "QToolButton{background:transparent;border:none;color:@infoText;font-size:8.5pt;"
        "text-align:left;padding:2px 6px;}"
        "QToolButton:hover{background:@hoverSoft;border-radius:6px;}");
    m_busy->setVisible(false);
    connect(m_busy, &QToolButton::clicked, this, [] { SilentInstallLog::showLog(); });

    m_busyTimer = new QTimer(this);
    m_busyTimer->setInterval(140);
    connect(m_busyTimer, &QTimer::timeout, this, &SettingsDialog::refreshBusy);
    m_busyTimer->start();
}

void SettingsDialog::refreshBusy()
{
    const int n = SilentInstallLog::busyCount();
    if (n <= 0) {
        if (m_busy->isVisible())
            m_busy->setVisible(false);
        return;
    }
    m_busyFrame = (m_busyFrame + 1) % 8;
    m_busy->setIcon(spinnerIcon(m_busyFrame));
    const QString text = QString("正在安装 %1 项…").arg(n);
    if (m_busy->text() != text)
        m_busy->setText(text);
    m_busy->setToolTip("后台静默安装进行中，点击打开安装日志（可取消安装）");
    m_busy->setVisible(true);
}

void SettingsDialog::ensurePage(int index)
{
    if (index < 0 || index >= 4)
        return;
    if (m_built[index]) {
        m_stack->setCurrentIndex(index);
        return;
    }
    m_built[index] = true;
    QWidget* page = nullptr;
    switch (index) {
    case 0: page = new WebsitesPage(this); break;
    case 1:
        m_toolsPage = new ToolsPage(this);
        page = m_toolsPage;
        break;
    case 2: {
        auto* configPage = new ConfigPage(this);
        connect(configPage, &ConfigPage::rescanRequested, this, &SettingsDialog::rescanRequested);
        connect(configPage, &ConfigPage::hotkeyChanged, this, &SettingsDialog::hotkeyChanged);
        page = configPage;
        break;
    }
    case 3: page = new AboutPage(this); break;
    }
    QWidget* old = m_stack->widget(index);
    m_stack->removeWidget(old);
    delete old;
    m_stack->insertWidget(index, page);
    m_stack->setCurrentIndex(index);
}

// 工具都是 DLL 插件，所以这里只剩一条路：把设置页开到「工具」那一行，再展开那张卡。
// 返回 false = 那个 id 没加载起来（ToolsPage 里已经写了一条带原因的日志）。
bool SettingsDialog::showTool(const QString& id, const QString& text)
{
    ensurePage(1);             // 「插件商店」那一行
    m_menu->setCurrentRow(1);
    return m_toolsPage && m_toolsPage->showTool(id, text);
}

void SettingsDialog::showEvent(QShowEvent* e)
{
    QDialog::showEvent(e);
    // Win10/11 深色标题栏
    HWND h = reinterpret_cast<HWND>(winId());
    BOOL dark = TRUE;
    DwmSetWindowAttribute(h, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    // 明确取消置顶并激活展示在前台
    SetWindowPos(h, HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(h);
}

void SettingsDialog::reject()
{
    // 按 Esc 键等同于关闭窗口，确保触发 closeEvent 与 WA_DeleteOnClose 彻底析构
    close();
}

void SettingsDialog::openAt(int menuRow, int tab, const QString& toolId)
{
    m_menu->setCurrentRow(qBound(0, menuRow, m_menu->count() - 1));
    // 顺序是"先展开卡、再挑 tab"：软件那两张页搬进插件之后，那个 QTabWidget 在卡片里面，
    // 反过来写就永远挑不到。
    if (!toolId.isEmpty() && m_toolsPage)
        m_toolsPage->showTool(toolId);
    if (tab >= 0) {
        QWidget* host = m_stack->widget(qBound(0, menuRow, m_stack->count() - 1));
        if (auto* tabs = host ? host->findChild<QTabWidget*>() : nullptr)
            tabs->setCurrentIndex(qBound(0, tab, tabs->count() - 1));
    }
}
