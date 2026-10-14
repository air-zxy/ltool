#include "SettingsDialog.h"

#include "ConfigPage.h"
#include "DownloadsPage.h"
#include "LanguagePage.h"
#include "SilentInstallLog.h"
#include "SoftwarePage.h"
#include "UninstallPage.h"
#include "TasksPage.h"
#include "AboutPage.h"
#include "ToolsPage.h"
#include "WebsitesPage.h"
#include "Utils.h"

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
QDialog { background: #1b1c20; }

QLabel { color: #e8ebf2; background: transparent; }
QLabel#tip { color: #8b93a1; }
QLabel#pageTitle { font-size: 14pt; font-weight: 600; }
QLabel#muted { color: #8b93a1; }

QGroupBox {
    color: #9aa3b2;
    border: 1px solid #2c2d34;
    border-radius: 10px;
    margin-top: 16px;
    padding: 10px 6px 6px 6px;
    font-weight: 600;
    background: #1f2025;
}
QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; }

QPlainTextEdit {
    background: #17181c;
    border: 1px solid #2c2d34;
    border-radius: 8px;
    color: #9fe0a8;
    font-family: Consolas, monospace;
    font-size: 9pt;
}
QLineEdit, QComboBox, QSpinBox, QTimeEdit, QKeySequenceEdit {
    background: #232429;
    border: 1px solid #33353d;
    border-radius: 8px;
    padding: 5px 8px;
    color: #e8ebf2;
    selection-background-color: #3f6ae0;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QTimeEdit:focus, QKeySequenceEdit:focus {
    border: 1px solid #3f6ae0;
}
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView {
    background: #232429;
    color: #e8ebf2;
    selection-background-color: #3f6ae0;
    border: 1px solid #33353d;
    outline: none;
}

QPushButton {
    background: #2b2d34;
    color: #e8ebf2;
    border: 1px solid #3a3c45;
    border-radius: 8px;
    padding: 6px 14px;
}
QPushButton:hover { background: #33353d; }
QPushButton:pressed { background: #26272c; }
QPushButton:disabled { color: #5b616e; background: #232429; }
QPushButton#primary {
    background: #3f6ae0;
    border: 1px solid #3f6ae0;
    color: white;
    font-weight: 600;
}
QPushButton#primary:hover { background: #4f79e8; }
QPushButton#primary:pressed { background: #3560cc; }
/* #primary 的 ID 选择器比 :disabled 优先，不补这条的话"禁用的主按钮"看着还是能点 */
QPushButton#primary:disabled { background: #232429; border: 1px solid #2c2d34; color: #5b616e; }
/* 列表行上的小动作按钮：一行四个，要紧凑，但禁用时必须一眼看出来 */
QPushButton#rowBtn {
    padding: 2px 9px;
    font-size: 8.5pt;
    border-radius: 6px;
}
QPushButton#rowBtn:disabled { background: transparent; border: 1px solid #2c2d34; color: #4d525d; }
/* 软件列表的表头：比正文淡、比说明文字重一点，好认但不抢 */
QLabel#colHead { color: #8b93a4; font-size: 8.5pt; font-weight: 600; background: transparent; }

QCheckBox { color: #e8ebf2; spacing: 8px; background: transparent; }
QCheckBox::indicator {
    width: 16px; height: 16px;
    border: 1px solid #3a3c45;
    border-radius: 4px;
    background: #232429;
}
QCheckBox::indicator:checked { background: #3f6ae0; border-color: #3f6ae0; }
QCheckBox::indicator:hover { border-color: #4f79e8; }

QListWidget {
    background: #1f2025;
    border: 1px solid #2c2d34;
    border-radius: 10px;
    color: #d7dbe4;
    outline: none;
}
/* 条目背景不能只靠 padding：Qt 的行高来自 sizeHint，padding 只会把画出来的
   背景块撑得比行还高，于是上下条目的背景互相压住（语言列表行距 29、背景 47，
   压掉 18px，看着就是"上下背景色挤在一起"）。行高在下面用 setSizeHint 给足，
   这里只留水平方向的呼吸和圆角。 */
QListWidget::item { padding: 0 8px; border-radius: 6px; }
QListWidget::item:selected { background: #3f6ae0; color: white; }
QListWidget::item:hover:!selected { background: #2a2b31; }

QTabWidget::pane { border: none; background: transparent; }
QTabBar::tab { background: #232429; color: #9aa3b2; border: 1px solid #2c2d34;
    padding: 7px 16px; margin-right: 4px; border-top-left-radius: 8px; border-top-right-radius: 8px; }
QTabBar::tab:selected { background: #2b2d34; color: #e8ebf2; border-bottom-color: #3f6ae0; }
QTabBar::tab:hover:!selected { background: #26272c; color: #d7dbe4; }

QScrollArea#cfgScroll, QScrollArea#cfgScroll > QWidget#qt_scrollarea_viewport { background: transparent; }
QWidget#cfgBody { background: transparent; }
QWidget#rowsHost { background: transparent; }
/* 列表分组表头（软件页）：不属于 #row 的盒子，只要一行小字 + 上方的呼吸 */
QLabel#catHead { color: #7d8698; font-weight: 600; font-size: 9pt; padding: 6px 2px 0 2px; }
QWidget#row, QWidget#storeBox, QWidget#storeRow {
    background: #1f2025;
    border: 1px solid #2c2d34;
    border-radius: 8px;
}
QWidget#row:hover, QWidget#storeBox:hover, QWidget#storeRow:hover { background: #24252b; border-color: #3a3c45; }
QWidget#row QLabel, QWidget#storeBox QLabel, QWidget#storeRow QLabel { background: transparent; }

QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }
QScrollBar::handle:vertical { background: #33353d; border-radius: 4px; min-height: 24px; }
QScrollBar::handle:vertical:hover { background: #43464f; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
)QSS";

} // namespace

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("ltool 设置");
    resize(1140, 790);
    setStyleSheet(kSettingsQss);

    m_menu = new QListWidget(this);
    m_menu->setObjectName("menuList");
    m_menu->setFixedWidth(150);
    m_menu->setSpacing(4);              // 条目之间留缝，背景块不再首尾相接
    m_menu->setUniformItemSizes(true);

    struct MenuDef { const char* name; QColor color; };
    const MenuDef defs[] = {
        {"语言", QColor(63, 106, 224)},   {"软件", QColor(46, 160, 88)},
        {"网站", QColor(31, 141, 160)},   {"下载器", QColor(13, 165, 170)},
        {"定时任务", QColor(214, 128, 15)},
        {"工具", QColor(120, 130, 150)},
        {"配置", QColor(150, 150, 160)},
        {"关于", QColor(90, 100, 120)},
    };
    for (const MenuDef& d : defs) {
        auto* it = new QListWidgetItem(Utils::letterIcon(d.name, d.color), d.name);
        it->setSizeHint(QSize(150, 44));   // 行高由这里定，QSS 的 padding 不再撑破邻居
        m_menu->addItem(it);
    }

    m_stack = new QStackedWidget(this);
    for (int i = 0; i < 8; ++i)
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
    m_busy->setStyleSheet(
        "QToolButton{background:transparent;border:none;color:#78c8ff;font-size:8.5pt;"
        "text-align:left;padding:2px 6px;}"
        "QToolButton:hover{background:rgba(255,255,255,26);border-radius:6px;}");
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
    if (index < 0 || index >= 8)
        return;
    if (m_built[index]) {
        m_stack->setCurrentIndex(index);
        return;
    }
    m_built[index] = true;
    QWidget* page = nullptr;
    switch (index) {
    case 0: page = new LanguagePage(this); break;
    case 1: {
        // 「软件」菜单现在是两个 tab：推荐软件（原来那份可一键安装的清单）
        // + 软件卸载（本机已装程序，调用它们自己的卸载程序）
        auto* hub = new QWidget(this);
        auto* hubLay = new QVBoxLayout(hub);
        hubLay->setContentsMargins(0, 0, 0, 0);
        hubLay->setSpacing(0);
        auto* tabs = new QTabWidget(hub);
        tabs->addTab(new SoftwarePage(tabs), QStringLiteral("推荐软件"));
        tabs->addTab(new UninstallPage(tabs), QStringLiteral("软件列表"));
        hubLay->addWidget(tabs);
        page = hub;
        break;
    }
    case 2: page = new WebsitesPage(this); break;
    case 3: page = new DownloadsPage(this); break;
    case 4: page = new TasksPage(this); break;
    case 5:
        m_toolsPage = new ToolsPage(this);
        page = m_toolsPage;
        break;
    case 6: {
        auto* configPage = new ConfigPage(this);
        connect(configPage, &ConfigPage::rescanRequested,
                this, &SettingsDialog::rescanRequested);
        connect(configPage, &ConfigPage::hotkeyChanged,
                this, &SettingsDialog::hotkeyChanged);
        page = configPage;
        break;
    }
    case 7: page = new AboutPage(this); break;
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
    ensurePage(5);             // 「工具」那一行
    m_menu->setCurrentRow(5);
    return m_toolsPage && m_toolsPage->showTool(id, text);
}

void SettingsDialog::showEvent(QShowEvent* e)
{
    QDialog::showEvent(e);
    // Win10/11 深色标题栏
    HWND h = reinterpret_cast<HWND>(winId());
    BOOL dark = TRUE;
    DwmSetWindowAttribute(h, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    // 明确取消置顶
    SetWindowPos(h, HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void SettingsDialog::openAt(int menuRow, int tab, const QString& toolId)
{
    m_menu->setCurrentRow(qBound(0, menuRow, m_menu->count() - 1));
    if (tab >= 0) {
        QWidget* page = m_stack->widget(qBound(0, menuRow, m_stack->count() - 1));
        if (auto* tabs = page ? page->findChild<QTabWidget*>() : nullptr)
            tabs->setCurrentIndex(qBound(0, tab, tabs->count() - 1));
    }
    if (!toolId.isEmpty() && m_toolsPage)
        m_toolsPage->showTool(toolId);
}
