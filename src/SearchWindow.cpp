#include "SearchWindow.h"

#include "CaptureHistory.h"
#include "ClipboardStore.h"
#include "DevEnv.h"
#include "EnvPath.h"
#include "FileTools.h"
#include "FileIndexer.h"
#include "SettingsDialog.h"
#include "SysTools.h"
#include "PluginHost.h"
#include "Pinyin.h"
#include "Utils.h"
#include "WebShortcuts.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QApplication>
#include <QEventLoop>
#include <QClipboard>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFontMetrics>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScreen>
#include <QSettings>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

#include <windows.h>

namespace {

constexpr int kWindowWidth = 660;
constexpr int kRowHeight = 54;
constexpr int kSectionHeight = 28;
constexpr int kMaxVisibleRows = 12;   // 空查询的常用面板有 5 节，9 行只够看到两节

QIcon letterIcon(const QString& text, const QColor& color)
{
    QPixmap pm(48, 48);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(2, 2, 44, 44, 12, 12);
    p.fillPath(path, color);
    QFont f("Microsoft YaHei UI", 17);
    f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(pm.rect(), Qt::AlignCenter, text.left(1));
    return QIcon(pm);
}

QColor colorForKind(const QString& key)
{
    static const QColor palette[] = {
        QColor(63, 106, 224), QColor(46, 160, 88), QColor(214, 128, 15),
        QColor(136, 87, 229), QColor(214, 69, 69), QColor(31, 141, 160),
    };
    const size_t h = std::hash<QString>{}(key);
    return palette[h % (sizeof(palette) / sizeof(palette[0]))];
}

class ResultDelegate : public QStyledItemDelegate {
public:
    explicit ResultDelegate(SearchWindow* w) : QStyledItemDelegate(w), m_w(w) {}

    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& index) const override
    {
        const int i = index.data(Qt::UserRole).toInt();
        p->save();
        p->setRenderHint(QPainter::Antialiasing);

        if (i < 0) { // 分组标题
            QFont f = opt.font;
            f.setPointSizeF(8.5);
            f.setBold(true);
            p->setFont(f);
            p->setPen(QColor(120, 128, 140));
            p->drawText(opt.rect.adjusted(10, 0, -8, 0),
                        Qt::AlignVCenter | Qt::AlignLeft, index.data().toString());
            p->restore();
            return;
        }

        const SearchResult& r = m_w->resultAt(i);
        const QRect rr = opt.rect.adjusted(8, 2, -8, 2);

        if (opt.state & QStyle::State_Selected) {
            QPainterPath path;
            path.addRoundedRect(rr, 9, 9);
            p->fillPath(path, QColor(255, 255, 255, 28));
        }

        const QRect iconRect(rr.left() + 8, rr.center().y() - 14, 28, 28);
        if (!r.icon.isNull()) {
            r.icon.paint(p, iconRect);
        } else {
            QPainterPath path;
            path.addEllipse(iconRect);
            p->fillPath(path, QColor(90, 96, 108));
            p->setPen(Qt::white);
            p->drawText(iconRect, Qt::AlignCenter, "?");
        }

        QFont f = opt.font;
        f.setPointSizeF(10.5);
        f.setBold(true);
        p->setFont(f);
        p->setPen(r.actionable ? QColor(235, 238, 245) : QColor(160, 166, 178));
        const QFontMetrics tfm(f);
        p->drawText(QRect(rr.left() + 48, rr.top() + 6, rr.width() - 58, 20),
                    Qt::AlignVCenter,
                    tfm.elidedText(r.title, Qt::ElideRight, rr.width() - 58));

        f.setBold(false);
        f.setPointSizeF(8.5);
        p->setFont(f);
        p->setPen(QColor(148, 155, 168));
        const QFontMetrics sfm(f);
        p->drawText(QRect(rr.left() + 48, rr.top() + 29, rr.width() - 58, 16),
                    Qt::AlignVCenter,
                    sfm.elidedText(r.subtitle, Qt::ElideMiddle, rr.width() - 58));
        p->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& index) const override
    {
        const int i = index.data(Qt::UserRole).toInt();
        return {opt.rect.width(), i < 0 ? kSectionHeight : kRowHeight};
    }

private:
    SearchWindow* m_w;
};

// 全角→半角：中文输入法在"全角"下打出来的是 ＱＱ（U+FF31），和索引里 ASCII 的 "qq"
// 一个字符都对不上，界面上就变成"搜 QQ 搜不到应用"（2026-10-12 他报的这条）。
// U+FF01–U+FF5E 是 U+0021–U+007E 各加 0xFEE0，全角空格是 U+3000。
QString halfWidth(const QString& s)
{
    bool needs = false;
    for (const QChar& c : s) {
        const ushort u = c.unicode();
        if ((u >= 0xFF01 && u <= 0xFF5E) || u == 0x3000) {
            needs = true;
            break;
        }
    }
    if (!needs)
        return s;                     // 绝大多数查询就是纯 ASCII，别每个键都重搭一遍串
    QString out;
    out.reserve(s.size());
    for (const QChar& c : s) {
        const ushort u = c.unicode();
        if (u >= 0xFF01 && u <= 0xFF5E)
            out.append(QChar(u - 0xFEE0));
        else if (u == 0x3000)
            out.append(QLatin1Char(' '));
        else
            out.append(c);
    }
    return out;
}

// 一条应用的"使用次数"记在哪个键上。以前直接用 lnkPath，而**目录扫出来的那些没有 lnk**，
// 于是它们全共用同一个空键：这台机器实测那个键涨到 43，QQEX（没有快捷方式）就靠这 43 次
// "别人家的记录"排到了 QQ 前面。没有 lnk 就用 target 当键，各记各的。
QString usageKey(const QString& lnkPath, const QString& target)
{
    return lnkPath.isEmpty() ? target : lnkPath;
}

int scoreApp(const AppEntry& a, const QString& low)
{
    const QString n = a.name.toLower();
    if (n.startsWith(low))
        return 0;
    const QStringList words = n.split(QRegularExpression("[\\s\\-_\\.]"), Qt::SkipEmptyParts);
    for (const QString& w : words)
        if (w.startsWith(low))
            return 1;
    // 拼音两档：renwu → 任务（全拼），rw → 任务/软件（首字母）。排在英文词首之后、
    // 子串之前 —— 打拼音的人要的就是那个中文名，比"名字里正好含这几个字母"更准。
    switch (Pinyin::match(a.name, low)) {
    case Pinyin::Full:
        return 2;
    case Pinyin::Initial:
        return 3;
    default:
        break;
    }
    if (n.contains(low))
        return 4;
    if (!a.target.isEmpty()
        && QFileInfo(a.target).fileName().toLower().contains(low))
        return 5;
    return -1;
}

} // namespace

SearchWindow::SearchWindow(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    loadUsage();
    m_apps = AppScanner::loadIndex();
    m_apps += SysTools::systemTools(); // Windows 内置面板/工具进索引

    connect(&FileIndexer::instance(), &FileIndexer::indexingFinished, this,
            [this](int total) {
                if (isVisible() && m_input->text().trimmed().isEmpty()) {
                    m_hint->setText(QString("索引完成：应用 %1 · 文件 %2")
                                        .arg(m_apps.size()).arg(total));
                }
            });
}

void SearchWindow::buildUi()
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose, false);

    m_input = new QLineEdit(this);
    m_input->setObjectName(QStringLiteral("input"));   // --pinyin 的端到端断言靠它找到输入框
    m_input->setFixedHeight(48);
    m_input->setPlaceholderText("搜索应用、文件、网址");
    m_input->setStyleSheet(
        "QLineEdit{background:transparent;border:none;color:#eef1f7;"
        "font-size:15pt;padding:4px 10px;}");
    m_input->installEventFilter(this);

    // 右侧齿轮：打开配置页面
    m_settingsBtn = new QToolButton(this);
    m_settingsBtn->setText("⚙");
    m_settingsBtn->setToolTip("设置");
    m_settingsBtn->setCursor(Qt::PointingHandCursor);
    m_settingsBtn->setFixedSize(40, 40);
    m_settingsBtn->setStyleSheet(
        "QToolButton{background:transparent;border:none;color:#9aa3b2;font-size:20px;}"
        "QToolButton:hover{color:#eef1f7;background:rgba(255,255,255,24);border-radius:8px;}");

    auto* inputRow = new QWidget(this);
    inputRow->setStyleSheet("background:transparent;");
    auto* inputLay = new QHBoxLayout(inputRow);
    inputLay->setContentsMargins(0, 0, 0, 0);
    inputLay->setSpacing(2);
    inputLay->addWidget(m_input, 1);
    inputLay->addWidget(m_settingsBtn);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("list"));
    m_list->setItemDelegate(new ResultDelegate(this));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setStyleSheet(
        "QListWidget{background:transparent;border:none;outline:none;font-size:10.5pt;}"
        "QListWidget::item{background:transparent;}"
        "QScrollBar:vertical{background:transparent;width:6px;margin:2px;}"
        "QScrollBar::handle:vertical{background:rgba(255,255,255,60);border-radius:3px;min-height:20px;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}"
        "QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent;}");
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) { openContextMenu(m_list->mapToGlobal(pos)); });
    connect(m_list, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { activateCurrent(); });

    m_hint = new QLabel(
        "Enter/双击 运行 · ↑↓ 选择 · Esc 隐藏 · Alt+Space 呼出 · 右键 更多操作", this);
    m_hint->setAlignment(Qt::AlignCenter);
    m_hint->setFixedHeight(24);
    m_hint->setStyleSheet("color:#79828f;background:transparent;font-size:8.5pt;");

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(14, 12, 14, 8);
    lay->setSpacing(4);
    lay->addWidget(inputRow);
    lay->addWidget(m_list, 1);
    lay->addWidget(m_hint);

    resize(kWindowWidth, 420);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(120);
    connect(m_debounce, &QTimer::timeout, this, &SearchWindow::runSearch);
    connect(m_input, &QLineEdit::textChanged, this, [this](const QString&) {
        scheduleSearch();
    });
    connect(m_settingsBtn, &QToolButton::clicked, this, &SearchWindow::openSettings);

    // 装了新软件搜不到：应用索引以前只在启动时扫一次，配置里那个"扫描间隔"也没人读。
    m_appRescan = new QTimer(this);
    m_appRescan->setSingleShot(true);
    m_appRescan->setInterval(4000);
    m_lastAutoScan.start();
    connect(m_appRescan, &QTimer::timeout, this, [this] {
        const qint64 since = m_lastAutoScan.elapsed();
        if (since < 60 * 1000) {
            // 节流只能延后、不能丢弃：丢掉的话"装完软件"这次变更就永远没人处理了
            m_appRescan->start(60 * 1000 - since + 500);
            return;
        }
        m_lastAutoScan.restart();
        startScan(true, false);
    });
    // F1 浮层抢走激活时搜索框要先站着别走：窗口一 hide，浮层的"窗口吸附"
    // 就找不到搜索框了（截图像素里有它、可鼠标就是吸不上去）。浮层一关就补收。
    m_captureHold = new QTimer(this);
    m_captureHold->setInterval(200);
    connect(m_captureHold, &QTimer::timeout, this, [this] {
        if (CaptureState::isActive())
            return;
        m_captureHold->stop();
        if (!isActiveWindow() && (!m_menu || !m_menu->isVisible()))
            hide();
    });

    const QSettings cfg(Utils::dataDir() + "/config.ini", QSettings::IniFormat);
    const int hours = cfg.value("index/refreshHours", 6).toInt();
    if (hours > 0) {
        m_periodicScan = new QTimer(this);
        m_periodicScan->setInterval(hours * 3600 * 1000);
        connect(m_periodicScan, &QTimer::timeout, this, [this] {
            m_lastAutoScan.restart();
            startScan(true, false);
        });
        m_periodicScan->start();
    }
    watchAppDirs();
}

// 监视快捷方式所在的目录（含子目录，深度与 AppScanner::scan 一致）。
// QFileSystemWatcher 不递归，所以开始菜单那两层树要逐个目录挂上去。
void SearchWindow::watchAppDirs()
{
    if (!m_appWatch) {
        m_appWatch = new QFileSystemWatcher(this);
        connect(m_appWatch, &QFileSystemWatcher::directoryChanged, this,
                [this](const QString& path) {
                    // 目录被删除重建后 Qt 会丢掉监视，补挂一次再防抖重扫
                    if (QDir(path).exists() && !m_appWatch->directories().contains(path))
                        m_appWatch->addPath(path);
                    m_appRescan->start();   // 变更成串到来时把重扫窗口往后推
                });
    }

    QStringList want;
    const auto collect = [&want](auto&& self, const QString& dir, int depth) -> void {
        want << dir;
        if (depth <= 0)
            return;
        QDir d(dir);
        for (const QFileInfo& fi : d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
            self(self, fi.absoluteFilePath(), depth - 1);
    };
    const QString desktop = Utils::knownFolderPath(Utils::KnownFolder::Desktop);
    const QString pubDesktop = Utils::knownFolderPath(Utils::KnownFolder::PublicDesktop);
    const QString userProgs = Utils::knownFolderPath(Utils::KnownFolder::UserStartMenuPrograms);
    const QString commonProgs = Utils::knownFolderPath(Utils::KnownFolder::CommonStartMenuPrograms);
    if (!desktop.isEmpty())
        collect(collect, desktop, 1);
    if (!pubDesktop.isEmpty())
        collect(collect, pubDesktop, 0);
    if (!userProgs.isEmpty())
        collect(collect, userProgs, 10);
    if (!commonProgs.isEmpty())
        collect(collect, commonProgs, 10);

    QStringList cur = m_appWatch->directories();
    QStringList toAdd;
    for (const QString& d : std::as_const(want))
        if (!cur.contains(d, Qt::CaseInsensitive))
            toAdd << d;
    QStringList gone;
    for (const QString& d : std::as_const(cur))
        if (!QDir(d).exists())
            gone << d;
    if (!gone.isEmpty())
        m_appWatch->removePaths(gone);
    if (!toAdd.isEmpty())
        m_appWatch->addPaths(toAdd);
    Utils::log(QString("apps: watching %1 shortcut dirs (%2 new)")
                   .arg(m_appWatch->directories().size()).arg(toAdd.size()));
}

void SearchWindow::openSettings()
{
    if (m_settingsDlg) {   // 已经开着就把它提到前面，不再开第二个
        m_settingsDlg->raise();
        m_settingsDlg->activateWindow();
        return;
    }
    hide(); // 打开设置前先收起搜索框，不遮挡设置页
    m_settingsOpen = true;
    // 不用 exec()：应用模态会让设置页开着时其它顶层窗口收不到鼠标事件 ——
    // "开着设置页按 F1 截不了图"就是这么来的（浮层能显示，但拖不动）。
    // 改成非模态，窗口销毁时再复位状态。不以置顶的启动器为父窗口，避免设置页继承置顶属性。
    auto* dlg = new SettingsDialog(nullptr);
    m_settingsDlg = dlg;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &SettingsDialog::rescanRequested, this, &SearchWindow::rescan);
    connect(dlg, &SettingsDialog::hotkeyChanged, this, &SearchWindow::hotkeyChanged);
    connect(dlg, &QObject::destroyed, this, [this] {
        m_settingsOpen = false;
    });
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

// 托盘那两项和搜索框的结果行都走这条：弹设置页 → 工具页 → 展开那个插件的页。
// 工具全搬进 DLL 插件之后，主程序里再没有 JsonTool/Base64Tool 这两个类可以 new 了。
void SearchWindow::openSettingsPage(int menuRow)
{
    openSettings();
    if (auto* dlg = qobject_cast<SettingsDialog*>(m_settingsDlg.data()))
        dlg->openAt(menuRow);
}

void SearchWindow::openTool(const QString& id, const QString& text)
{
    openSettings();
    if (auto* dlg = qobject_cast<SettingsDialog*>(m_settingsDlg.data()))
        dlg->showTool(id, text);
}

// ---------- 显示/隐藏 ----------

void SearchWindow::showLauncher()
{
    // 设置页现在是非模态的，热键随时能把搜索框弹出来（以前模态时弹出来也打不了字）
    m_showLatency.start();
    m_paintLogged = false;
    const int wsBefore = Utils::workingSetMB();
    QElapsedTimer lap;
    lap.start();
    QScreen* s = QGuiApplication::screenAt(QCursor::pos());
    if (!s)
        s = QGuiApplication::primaryScreen();
    const QRect g = s->availableGeometry();
    move(g.center().x() - width() / 2, g.top() + int(g.height() * 0.20));
    m_input->clear(); // 再次呼出不保留上一次的搜索
    m_showFavorites = QSettings(Utils::dataDir() + "/config.ini", QSettings::IniFormat)
                          .value("ui/favoritesOnEmpty", false).toBool();
    updateHeight();
    const qint64 msLayout = lap.elapsed();
    lap.restart();
    // 被最小化过（或 Win+D 之后）单纯 show() 不会把它从最小化状态拉回来，
    // 表现同样是"按了热键没出来"，所以这里显式清掉最小化位
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    show();
    const qint64 msShow = lap.elapsed();
    raise();
    // 后台进程调用 SetForegroundWindow 会被 Windows 前台锁拦住，
    // 附加到当前前台线程的输入队列后再置前，保证弹出即可输入
    HWND h = reinterpret_cast<HWND>(winId());
    HWND fg = GetForegroundWindow();
    m_prevFg = reinterpret_cast<WId>(fg);   // Ctrl+Enter 粘贴要还给这个窗口
    DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    const DWORD curThread = GetCurrentThreadId();
    const bool attached = fgThread && fgThread != curThread
                          && AttachThreadInput(curThread, fgThread, TRUE);
    SetForegroundWindow(h);
    activateWindow();
    if (attached)
        AttachThreadInput(curThread, fgThread, FALSE);
    // 只在没抢到前台时留痕：那时窗口是"已显示但在别的窗口后面"，
    // 用户视角就是"按了热键没出来"，日志里必须能看出来是这一步而不是热键没到
    if (GetForegroundWindow() != h)
        Utils::log(QString("show: 置前失败（SetForegroundWindow 没抢到，前台仍是 0x1）")
                       .arg(quintptr(GetForegroundWindow()), 0, 16));
    m_input->setFocus();
    const qint64 msFg = lap.elapsed();
    lap.restart();
    m_showTime.start();
    runSearch();
    // 只在真的慢下来时才留痕（正常是 2-3ms，每次按热键都写一行会把日志刷满）
    if (m_showLatency.elapsed() > 30)
        Utils::log(QString("show: 慢启动器 布局=%1 建窗口=%2 置前=%3 结果=%4 合计=%5ms 工作集 %6→%7MB")
                       .arg(msLayout).arg(msShow).arg(msFg).arg(lap.elapsed()).arg(m_showLatency.elapsed())
                       .arg(wsBefore).arg(Utils::workingSetMB()));
}

void SearchWindow::warmUp()
{
    if (m_warmed || isVisible())
        return;
    m_warmed = true;
    // 第一次 show() 要付 Qt 建原生窗口 + DWM 首帧 + 字体/委托首次绘制的钱（实测约 300ms），
    // 那笔钱本来全落在用户第一次按热键上。挪到所有显示器之外映射一遍再收起，界面看不到。
    const QPoint saved = pos();
    // 两道保险：位置挪到显示器之外（Qt 有时会把它夹回屏幕内），同时整窗透明度压到 0
    setWindowOpacity(0.0);
    move(-32000, -32000);
    show();
    repaint();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    hide();
    setWindowOpacity(1.0);
    move(saved);
    Utils::log(QStringLiteral("warmup: 预热时窗口矩形=(%1,%2 %3x%4)（应在所有显示器之外）")
                   .arg(x()).arg(y()).arg(width()).arg(height()));
}

void SearchWindow::toggleVisible()
{
    // 以前这里只看 isVisible()，会出"有时候按了没反应"：这个窗口失焦不自动收起，
    // 于是它完全可能正躲在别的窗口后面（用户看不见）。那种状态下再按热键，
    // 旧逻辑走的是 hide() —— 按一下没出来，得再按一遍才出来。
    // 正确的判据是"它就是当前前台窗口"才收起，否则一律呼出并置前。
    // 注意最小化那一支：实测窗口被最小化之后 GetForegroundWindow() 仍然算它自己，
    // 所以"在前台"必须同时要求"没被最小化"，否则按热键会变成把它 hide 掉（--toggle 抓到过）。
    const bool visible = isVisible();
    const bool minimized = (windowState() & Qt::WindowMinimized) != 0;
    const bool fgIsMine = GetForegroundWindow() == reinterpret_cast<HWND>(winId());
    const bool onTop = visible && !minimized && fgIsMine;
    Utils::log(QStringLiteral("hotkey: %1（visible=%2 最小化=%3 前台=%4 位置=%5,%6 %7x%8 屏幕=%9 距上次呼出=%10ms）")
                   .arg(onTop ? QStringLiteral("收起") : QStringLiteral("呼出"))
                   .arg(visible ? 1 : 0).arg(minimized ? 1 : 0).arg(fgIsMine ? 1 : 0)
                   .arg(x()).arg(y()).arg(width()).arg(height())
                   .arg(QGuiApplication::screenAt(geometry().center()) ? QStringLiteral("有")
                                                                       : QStringLiteral("无"))
                   // 这一列是用来判"两次按压被吞"的：间隔只有两三百毫秒的一对，
                   // 说明第一次还堵在消息队列里，第二次落地就成了 show 完立刻 hide（=用户看到"没出来"）
                   .arg(m_showTime.isValid() ? m_showTime.elapsed() : -1));
    if (onTop)
        hide();
    else
        showLauncher();
}

void SearchWindow::updateHeight()
{
    int rows = 0;
    const int n = qMin(m_list->count(), kMaxVisibleRows);
    for (int i = 0; i < n; ++i) {
        const int idx = m_list->item(i)->data(Qt::UserRole).toInt();
        rows += (idx < 0 ? kSectionHeight : kRowHeight);
    }
    if (rows == 0)
        rows = (m_input && m_input->text().trimmed().isEmpty())
                   ? 0                    // 空查询又没列表（配置里关掉了）：别留一条空行的高度
                   : kRowHeight;          // 输入中还没出结果：先占一行，免得窗口跳来跳去
    // 边距 12+8 + 输入框 48 + 间距*2 + 提示栏 24
    resize(kWindowWidth, 12 + 48 + 4 + rows + 4 + 24 + 8);
}

void SearchWindow::setHotkeyLabel(const QString& label)
{
    if (label.isEmpty()) {
        m_hint->setText("Enter/双击 运行 · ↑↓ 选择 · Esc 隐藏 · 右键 更多操作");
    } else {
        m_hint->setText(QString("%1 呼出/隐藏 · Enter/双击 运行 · ↑↓ 选择 · Esc 隐藏 · 右键 更多操作")
                            .arg(label));
    }
}

void SearchWindow::paintEvent(QPaintEvent*)
{
    if (!m_paintLogged && m_showLatency.isValid()) {
        m_paintLogged = true;
        const qint64 ms = m_showLatency.elapsed();
        if (ms > 30)
            Utils::log(QStringLiteral("show: 慢启动器 首次绘制 +%1ms").arg(ms));
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(QRectF(0.5, 0.5, width() - 1, height() - 1), 13, 13);
    p.fillPath(path, QColor(28, 30, 36, 247));
    p.setPen(QPen(QColor(255, 255, 255, 26), 1));
    p.drawPath(path);
}

bool SearchWindow::event(QEvent* e)
{
    if (e->type() == QEvent::WindowDeactivate) {
        // 弹出后极短时间内的失焦是激活竞争，忽略；右键菜单打开时也不隐藏。
        // 设置页现在是非模态的，点去设置页时搜索框照样该收起（以前模态时不能这么判）
        const bool grace = m_showTime.isValid() && m_showTime.elapsed() < 500;
        if (!grace && (!m_menu || !m_menu->isVisible())) {
            if (m_captureHold && CaptureState::isActive()) {
                if (!m_captureHold->isActive())
                    m_captureHold->start();   // 截图结束再收（见构造函数里的说明）
            } else {
                hide();
            }
        } else if (grace) {
            // 宽限期里丢掉的这条失焦，以前没人再回头看，于是窗口能一直"可见但躲在别的程序后面"。
            // 那个状态本身没坏处，坏在它是"看不见却还活着"——下一次按热键的判据全靠它。
            // 所以宽限一过自己复查：确实不是活动窗口就收起。
            QTimer::singleShot(600, this, [this] {
                if (!isVisible() || isActiveWindow())
                    return;
                if (m_menu && m_menu->isVisible())
                    return;
                if (m_captureHold && CaptureState::isActive())
                    return;
                Utils::log(QStringLiteral("show: 失焦落在宽限里，复查后收起"));
                hide();
            });
        }
    }
    return QWidget::event(e);
}

bool SearchWindow::eventFilter(QObject* obj, QEvent* e)
{
    if (obj == m_input && e->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(e);
        switch (ke->key()) {
        case Qt::Key_Escape:
            hide();
            return true;
        case Qt::Key_Up:
            moveCurrent(-1);
            return true;
        case Qt::Key_Down:
            moveCurrent(1);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            // Ctrl+Enter 在剪贴板条目上多一步"顺手粘出去"
            if ((ke->modifiers() & Qt::ControlModifier) && pasteClipToPrevious())
                return true;
            activateCurrent();
            return true;
        default:
            break;
        }
    }
    return QWidget::eventFilter(obj, e);
}

// ---------- 结果构建 ----------

void SearchWindow::scheduleSearch()
{
    m_debounce->start();
}

void SearchWindow::addSection(const QString& text)
{
    SearchResult r;
    r.kind = SearchResult::Info;
    r.actionable = false;
    r.isSection = true;
    r.title = text;
    m_results.append(r);
}

void SearchWindow::addItem(const SearchResult& r)
{
    m_results.append(r);
}

void SearchWindow::rebuildList()
{
    m_list->clear();
    int firstActionable = -1;
    for (int i = 0; i < m_results.size(); ++i) {
        const SearchResult& r = m_results[i];
        QListWidgetItem* it = new QListWidgetItem(m_list);
        if (r.isSection) {
            it->setData(Qt::UserRole, -1);
            it->setText(r.title);
        } else {
            it->setData(Qt::UserRole, i);
        }
        if (firstActionable < 0 && r.actionable && !r.isSection)
            firstActionable = i;
    }
    if (firstActionable >= 0) {
        m_list->setCurrentRow(firstActionable);
        // setCurrentRow 会把当前行滚到视口顶部，于是第一节的表头被推出屏幕
        // （常用面板看起来少了"网站"那一节）。选中保留，滚动条拉回顶部。
        m_list->verticalScrollBar()->setValue(m_list->verticalScrollBar()->minimum());
    }
    // 空查询又没有列表（配置里关掉了"呼出时显示常用列表"）：连控件一起藏掉，
    // 不然它自己的最小高度会把窗口撑住，缩不成"只有一个搜索框"
    m_list->setVisible(!(m_list->count() == 0 && m_input->text().trimmed().isEmpty()));
    updateHeight();
    updateHeight();
}

void SearchWindow::runSearch()
{
    ++m_searchSeq;
    m_results.clear();
    m_regIssues.clear();

    const QString q = halfWidth(m_input->text()).trimmed();
    const QString low = q.toLower();

    // 一行工具结果：空查询的"常用"面板和搜工具时共用。
    // 索引指的是 PluginHost::scan() 里那一条 —— 九个自带工具现在也是插件，这里没有特判。
    const auto addToolRow = [this](int i) {
        const PluginHost::Meta& td = PluginHost::scan()[i];
        SearchResult r;
        r.kind = SearchResult::Command;
        r.title = QStringLiteral("打开 %1").arg(td.name);
        r.subtitle = QStringLiteral("%1 · DLL 工具（点开才加载）").arg(td.desc);
        r.target = QStringLiteral("tool:%1").arg(td.id);
        r.icon = letterIcon(td.name, td.color);
        addItem(r);
    };

    // 插件目录里"通过检查"的那几条的下标，卡片墙和这里共用同一个顺序。
    // 没通过的不在结果里（工具页那面会单独把原因写出来），但也不能因为它就少一张卡。
    QVector<int> toolIndices;
    {
        const auto& metas = PluginHost::scan();
        for (int i = 0; i < metas.size(); ++i)
            if (metas[i].ok())
                toolIndices << i;
    }

    if (q.isEmpty()) {
        if (!m_showFavorites) {
            // 配置里没勾"呼出时显示常用列表"：只留一个空搜索框，输入才出结果
            rebuildList();
            return;
        }
        // 空查询 = 常用面板：应用 / 网站 / 工具 / 文件 / 系统，各自按使用次数排
        const auto byUsage = [this](const QString& key) { return m_usage.value(key, 0); };

        QVector<int> sysOrder, appOrder;
        for (int i = 0; i < m_apps.size(); ++i)
            (m_apps[i].category == QLatin1String("system") ? sysOrder : appOrder).append(i);
        const auto rankApps = [&](QVector<int>& order) {
            std::sort(order.begin(), order.end(), [&](int a, int b) {
                const int ua = byUsage(m_apps[a].lnkPath);
                const int ub = byUsage(m_apps[b].lnkPath);
                if (ua != ub)
                    return ua > ub;
                return m_apps[a].name.compare(m_apps[b].name, Qt::CaseInsensitive) < 0;
            });
        };
        rankApps(sysOrder);
        rankApps(appOrder);

        const auto addAppRow = [this](int i) {
            const AppEntry& a = m_apps[i];
            SearchResult r;
            r.kind = SearchResult::App;
            r.title = a.name;
            r.subtitle = a.target.isEmpty() ? a.lnkPath : a.target;
            r.target = a.target;
            r.extra = a.lnkPath;
            if (!a.iconFile.isEmpty())
                r.icon = QIcon(a.iconFile);
            else if (a.category == QLatin1String("system"))
                r.icon = Utils::letterIcon(a.name, QColor(31, 141, 160));
            addItem(r);
        };

        addSection("── 应用 ──");
        if (appOrder.isEmpty()) {
            SearchResult r;
            r.kind = SearchResult::Info;
            r.actionable = false;
            r.title = "尚未扫描到应用，输入 rescan 并回车";
            addItem(r);
        }
        for (int k = 0; k < appOrder.size() && k < 6; ++k)
            addAppRow(appOrder[k]);

        const QVector<WebEntry> webs = WebShortcuts::loadWebs();
        QVector<int> webOrder(webs.size());
        for (int i = 0; i < webOrder.size(); ++i)
            webOrder[i] = i;
        std::sort(webOrder.begin(), webOrder.end(), [&](int a, int b) {
            const int ua = byUsage(QStringLiteral("web:") + webs[a].name);
            const int ub = byUsage(QStringLiteral("web:") + webs[b].name);
            if (ua != ub)
                return ua > ub;
            return webs[a].name.compare(webs[b].name, Qt::CaseInsensitive) < 0;
        });
        if (!webs.isEmpty()) {
            addSection("── 网站 ──");
            for (int k = 0; k < webOrder.size() && k < 4; ++k) {
                const WebEntry& w = webs[webOrder[k]];
                SearchResult r;
                r.kind = SearchResult::Web;
                r.title = w.name;
                r.subtitle = w.url;
                r.target = w.url;
                r.icon = letterIcon(w.name, QColor(31, 141, 160));
                addItem(r);
            }
        }

        addSection("── 工具 ──");
        for (const int i : toolIndices)
            addToolRow(i);

        // 文件：只列真正打开过的（usage 里以 file: 前缀记录），并且现在还在
        QVector<QPair<QString, int>> files;   // path, count
        for (auto it = m_usage.constBegin(); it != m_usage.constEnd(); ++it) {
            if (!it.key().startsWith(QLatin1String("file:")))
                continue;
            const QString path = it.key().mid(5);
            if (QFileInfo::exists(path))
                files.append({path, it.value()});
        }
        std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
            return a.second != b.second ? a.second > b.second : a.first < b.first;
        });
        if (!files.isEmpty()) {
            addSection("── 文件 ──");
            for (int k = 0; k < files.size() && k < 5; ++k) {
                const QString& path = files[k].first;
                SearchResult r;
                r.kind = SearchResult::File;
                r.title = QFileInfo(path).fileName();
                r.subtitle = path;
                r.target = path;
                r.icon = letterIcon(r.title, QColor(120, 130, 150));
                addItem(r);
            }
        }

        if (!sysOrder.isEmpty()) {
            addSection("── 系统 ──");
            for (int k = 0; k < sysOrder.size() && k < 4; ++k)
                addAppRow(sysOrder[k]);
        }
        rebuildList();
        return;
    }

    // 命令区域：init / path / reg / rescan
    const QString tok0 = q.section(' ', 0, 0).toLower();
    const QString rest = q.mid(tok0.size()).trimmed();

    if (tok0 == "init") {
        handleInit(rest);
        rebuildList();
        return;
    }
    if (tok0 == "path") {
        handlePath(rest);
        rebuildList();
        return;
    }
    if (tok0 == "reg") {
        handleReg(rest);
        rebuildList();
        return;
    }

    // rescan 命令行
    if (tok0 == "rescan") {
        SearchResult r;
        r.kind = SearchResult::Command;
        r.title = "rescan — 重新扫描应用与文件索引";
        r.subtitle = "按 Enter 开始（桌面/开始菜单快捷方式 + 常用目录文件索引）";
        r.target = "rescan";
        r.icon = letterIcon("⟳", QColor(46, 160, 88));
        addItem(r);
        rebuildList();
        return;
    }

    // json 命令行（带内容）：直接把那串递给 JSON 工具那张页解析
    if (tok0 == "json" && !q.mid(tok0.size()).trimmed().isEmpty()) {
        SearchResult r;
        r.kind = SearchResult::Command;
        r.title = "json — 把后面这串丢进 JSON 格式化";
        const QString payload = q.mid(tok0.size()).trimmed();
        r.subtitle = QStringLiteral("按 Enter 打开工具页并解析 %1 个字符").arg(payload.size());
        r.target = "tool:json";
        r.extra = payload;
        r.icon = letterIcon("{}", QColor(90, 140, 220));
        addItem(r);
        rebuildList();
        return;
    }

    // base64 命令行（带内容）：直接把那串递给编解码那张页
    if ((tok0 == "b64" || tok0 == "base64") && !q.mid(tok0.size()).trimmed().isEmpty()) {
        SearchResult r;
        r.kind = SearchResult::Command;
        r.title = "b64 — 把后面这串拿去解码";
        const QString payload = q.mid(tok0.size()).trimmed();
        r.subtitle = QStringLiteral("按 Enter 打开工具页并解码 %1 个字符").arg(payload.size());
        r.target = "tool:base64";
        r.extra = payload;
        r.icon = letterIcon("=", QColor(190, 130, 60));
        addItem(r);
        rebuildList();
        return;
    }

    // 剪贴板历史：clip 列出最近的条目，Enter 放回剪贴板，Ctrl+Enter 顺手粘出去
    if (tok0 == "clip" || low.contains(QStringLiteral("剪贴板"))) {
        auto& cs = ClipboardStore::instance();
        addSection(QStringLiteral("剪贴板历史"));
        if (!cs.enabled()) {
            SearchResult r;
            r.kind = SearchResult::Command;
            r.title = QStringLiteral("开启剪贴板记录");
            r.subtitle = QStringLiteral("现在没在记录（设置 → 配置 → 剪贴板 里也能关）。开启后复制过的文本和图片会被收进本地历史。");
            r.target = "clip:on";
            r.icon = letterIcon(QStringLiteral("刷"), QColor(190, 130, 60));
            addItem(r);
        } else if (cs.items().isEmpty()) {
            SearchResult r;
            r.kind = SearchResult::Command;
            r.title = QStringLiteral("还没有记录：复制点东西再回来");
            r.subtitle = QStringLiteral("上限 200 条；超过 256 KB 的文本不收");
            r.target = "clip:none";
            r.actionable = false;
            addItem(r);
        }
        int n = 0;
        for (const ClipboardStore::Item& it : cs.items()) {
            if (n >= 15)
                break;
            SearchResult r;
            r.kind = SearchResult::Command;
            r.title = ClipboardStore::summary(it);
            r.subtitle = QStringLiteral("%1 · Enter 放回剪贴板，Ctrl+Enter 直接粘贴")
                             .arg(it.when.toString("MM-dd hh:mm"));
            r.target = QStringLiteral("clip:%1").arg(n);
            QIcon ic = cs.iconFor(it);
            r.icon = ic.isNull() ? letterIcon(QStringLiteral("剪"), QColor(150, 110, 220)) : ic;
            addItem(r);
            ++n;
        }
        if (cs.enabled() && !cs.items().isEmpty()) {
            SearchResult r;
            r.kind = SearchResult::Command;
            r.title = QStringLiteral("清空剪贴板历史");
            r.subtitle = QStringLiteral("删除 %1 条记录（含图片文件）").arg(cs.items().size());
            r.target = "clip:clear";
            r.icon = letterIcon(QStringLiteral("×"), QColor(214, 69, 69));
            addItem(r);
        }
        rebuildList();
        return;
    }

    // 工具直达：卡片墙那些事，搜名字或关键词都能打开。
    // 现在九个自带工具和别人写的插件是同一张表（PluginHost::scan()），这里没有"内置"特判。
    // 两条规矩是从「计划任务抢页」那次教训直接抄过来的：
    // ① 命中只认开头（matchStart）—— 中段撞上是"名字里正好含这几个字母"，
    //    拿它独占整页会把用户要的那个顶掉（搜 renwu 想要任务管理器，却被「开机启动分析」
    //    的关键词"计划任务"当成直达）；
    // ② 只有"整条查询就是这个词"才 return 独占，否则只在末尾追加一节 ——
    //    不然"下载""网络"这种常用词会把应用列表挤掉。
    QVector<int> toolLoose;
    {
        const auto& metas = PluginHost::scan();
        if (low == QStringLiteral("工具") || low == QStringLiteral("tools")
            || low == QStringLiteral("工具箱")) {   // 想看全部就搜"工具"
            addSection(QStringLiteral("工具"));
            for (const int i : std::as_const(toolIndices))
                addToolRow(i);
            rebuildList();
            return;
        }
        QVector<int> exact;
        for (const int i : std::as_const(toolIndices)) {
            const PluginHost::Meta& td = metas[i];
            const QString name = td.name.toLower();
            bool hitExact = (low == name);
            bool hitLoose = low.contains(name);
            for (const QString& kw : td.keywords) {
                const QString k = kw.toLower();
                if (k.isEmpty())
                    continue;
                if (low == k || low.startsWith(k + QLatin1Char(' ')))
                    hitExact = true;
                if (low.contains(k))
                    hitLoose = true;
            }
            // 拼音直达：cipanqingli / cpql 也算"就要这个工具"。工具就卡片墙那几件事，
            // 命中面比应用列表窄得多，所以首字母也当直达（应用那边首字母只排第三档）。
            if (!hitExact && Pinyin::usableQuery(low)) {
                if (Pinyin::matchStart(td.name, low) != Pinyin::None)
                    hitExact = true;
                else
                    for (const QString& kw : td.keywords)
                        if (Pinyin::matchStart(kw, low) != Pinyin::None) {
                            hitExact = true;
                            break;
                        }
            }
            if (hitExact)
                exact << i;
            else if (hitLoose)
                toolLoose << i;
        }
        if (!exact.isEmpty()) {
            addSection(QStringLiteral("工具"));
            for (const int i : std::as_const(exact))
                addToolRow(i);
            rebuildList();
            return;
        }
    }

    // 网站关键字 / 网址直开
    const QVector<WebEntry> webs = WebShortcuts::loadWebs();
    const QString webTok = low.section(' ', 0, 0);
    const QString webArgs = q.mid(webTok.size()).trimmed();
    for (const WebEntry& w : webs) {
        if (!w.keys.contains(webTok) && w.name.compare(webTok, Qt::CaseInsensitive) != 0)
            continue;
        SearchResult r;
        r.kind = SearchResult::Web;
        r.title = w.name;
        r.subtitle = QString("打开官网 · %1").arg(w.url);
        r.target = w.url;
        r.icon = letterIcon(w.name, colorForKind(w.url));
        addItem(r);
        if (!webArgs.isEmpty() && !w.searchUrl.isEmpty()) {
            SearchResult s;
            s.kind = SearchResult::Web;
            s.title = QString("在 %1 搜索 “%2”").arg(w.name, webArgs);
            s.subtitle = w.searchUrl;
            s.target = QString(w.searchUrl).replace("%s", QString::fromUtf8(QUrl::toPercentEncoding(webArgs)));
            s.icon = letterIcon("搜", QColor(63, 106, 224));
            addItem(s);
        }
        break;
    }
    if (WebShortcuts::looksLikeUrl(q)) {
        SearchResult r;
        r.kind = SearchResult::Web;
        r.title = WebShortcuts::normalizeUrl(q);
        r.subtitle = "在默认浏览器打开网址";
        r.target = WebShortcuts::normalizeUrl(q);
        r.icon = letterIcon("网", QColor(31, 141, 160));
        addItem(r);
    }

    // 应用搜索
    struct Match { int score; int idx; };    QVector<Match> ms;
    for (int i = 0; i < m_apps.size(); ++i) {
        const int s = scoreApp(m_apps[i], low);
        if (s >= 0)
            ms.append({s, i});
    }
    std::sort(ms.begin(), ms.end(), [this](const Match& a, const Match& b) {
        if (a.score != b.score)
            return a.score < b.score;
        const int ua = m_usage.value(usageKey(m_apps[a.idx].lnkPath, m_apps[a.idx].target), 0);
        const int ub = m_usage.value(usageKey(m_apps[b.idx].lnkPath, m_apps[b.idx].target), 0);
        if (ua != ub)
            return ua > ub;
        return m_apps[a.idx].name.size() < m_apps[b.idx].name.size();
    });

    if (!ms.isEmpty())
        addSection("── 应用 ──");
    const int appLimit = qMin(8, ms.size());
    for (int k = 0; k < appLimit; ++k) {
        const AppEntry& a = m_apps[ms[k].idx];
        SearchResult r;
        r.kind = SearchResult::App;
        r.title = a.name;
        r.subtitle = a.target.isEmpty() ? a.lnkPath : a.target;
        r.target = a.target;
        r.extra = a.lnkPath;
        if (!a.iconFile.isEmpty())
            r.icon = QIcon(a.iconFile);
        else if (a.category == "system")
            r.icon = Utils::letterIcon(a.name, QColor(31, 141, 160));
        addItem(r);
    }

    if (!toolLoose.isEmpty()) {
        addSection(QStringLiteral("工具"));
        for (const int i : std::as_const(toolLoose))
            addToolRow(i);
    }



    // 文件搜索（≥2 个字符才搜，避免太泛）：丢到线程池，结果稍后插回列表
    if (low.size() >= 2)
        startFileSearch(q);

    // 网络搜索兜底（始终提供）
    if (low.size() >= 2) {
        SearchResult r;
        r.kind = SearchResult::Web;
        r.title = QString("用搜索引擎搜索 “%1”").arg(q);
        r.subtitle = WebShortcuts::engineSearchUrl(q);
        r.target = WebShortcuts::engineSearchUrl(q);
        r.icon = letterIcon("搜", QColor(136, 87, 229));
        addItem(r);
    }

    if (m_results.isEmpty()) {
        SearchResult r;
        r.kind = SearchResult::Info;
        r.actionable = false;
        r.title = "无匹配结果";
        addItem(r);
    }
    rebuildList();
}

// 深索引有数百万条，一次扫描上百毫秒：放到线程池里跑，输入不卡；
// 同一时刻只允许一个在跑，跑完若期间又换了查询就用最新输入重来一次
void SearchWindow::startFileSearch(const QString& q)
{
    if (m_fileSearchBusy) {
        m_fileSearchRerun = true;
        return;
    }
    m_fileSearchBusy = true;
    const quint64 seq = m_searchSeq;

    auto* watcher = new QFutureWatcher<QVector<FileEntry>>(this);
    connect(watcher, &QFutureWatcher<QVector<FileEntry>>::finished, this,
            [this, watcher, seq, q] {
                const auto files = watcher->result();
                watcher->deleteLater();
                m_fileSearchBusy = false;
                if (m_fileSearchRerun) {
                    m_fileSearchRerun = false;
                    runSearch();
                    return;
                }
                if (seq != m_searchSeq || halfWidth(m_input->text()).trimmed() != q)
                    return;   // 查询已变，结果作废
                if (files.isEmpty())
                    return;

                for (int i = m_results.size() - 1; i >= 0; --i)
                    if (m_results[i].kind == SearchResult::Info
                        && m_results[i].title == QStringLiteral("无匹配结果"))
                        m_results.removeAt(i);
                int pos = m_results.size();
                // 插在"用搜索引擎搜索…"那条兜底行之前 = 应用/网站/工具之后。
                // 以前是找第一条 Web 行，于是命中网站快捷方式时文件会被顶到最上面。
                const QString fallbackUrl = WebShortcuts::engineSearchUrl(q);
                for (int i = 0; i < m_results.size(); ++i) {
                    if (m_results[i].kind == SearchResult::Web
                        && m_results[i].target == fallbackUrl) {
                        pos = i;
                        break;
                    }
                }
                SearchResult head;
                head.kind = SearchResult::Info;
                head.actionable = false;
                head.isSection = true;
                head.title = QStringLiteral("── 文件 ──");
                m_results.insert(pos++, head);
                for (const FileEntry& fe : files) {
                    SearchResult r;
                    r.kind = SearchResult::File;
                    r.title = fe.name;
                    r.subtitle = fe.path;
                    r.target = fe.path;
                    r.icon = Utils::cachedFileIcon(fe.path);
                    m_results.insert(pos++, r);
                }
                rebuildList();
            });
    watcher->setFuture(QtConcurrent::run(
        [q]() { return FileIndexer::instance().search(q, 8); }));
}

// ---------- 激活 ----------

int SearchWindow::firstActionableRow() const
{
    for (int i = 0; i < m_list->count(); ++i) {
        const int idx = m_list->item(i)->data(Qt::UserRole).toInt();
        if (idx >= 0 && idx < m_results.size() && m_results[idx].actionable
            && m_results[idx].kind != SearchResult::Info)
            return i;
    }
    return -1;
}

void SearchWindow::moveCurrent(int dir)
{
    int row = m_list->currentRow();
    if (row < 0)
        row = firstActionableRow();
    int steps = m_list->count();
    while (steps-- > 0) {
        row += dir;
        if (row < 0 || row >= m_list->count())
            return;
        const int idx = m_list->item(row)->data(Qt::UserRole).toInt();
        if (idx >= 0 && idx < m_results.size() && m_results[idx].actionable
            && m_results[idx].kind != SearchResult::Info) {
            m_list->setCurrentRow(row);
            return;
        }
    }
}

void SearchWindow::activateCurrent()
{
    const int row = m_list->currentRow();
    if (row < 0)
        return;
    activateRow(row);
}

QString SearchWindow::currentCommandTarget() const
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_list->count())
        return QString();
    const int idx = m_list->item(row)->data(Qt::UserRole).toInt();
    if (idx < 0 || idx >= m_results.size())
        return QString();
    const SearchResult& r = m_results[idx];
    return r.kind == SearchResult::Command ? r.target : QString();
}

// 放回剪贴板后，把输入焦点还给呼出启动器之前的那个窗口，再补一次 Ctrl+V。
// 不这么做的话"从历史里挑一条"还要再手动按一次粘贴。
bool SearchWindow::pasteClipToPrevious()
{
    const QString target = currentCommandTarget();
    if (!target.startsWith(QLatin1String("clip:")))
        return false;
    bool ok = false;
    const int n = target.mid(5).toInt(&ok);
    if (!ok)
        return false;
    ClipboardStore::instance().restore(n);
    hide();
    auto* fg = reinterpret_cast<HWND>(m_prevFg);
    if (!fg || !IsWindow(fg))
        return true;                      // 找不到原窗口就只放回剪贴板，别乱发按键
    const DWORD fgThread = GetWindowThreadProcessId(fg, nullptr);
    const DWORD cur = GetCurrentThreadId();
    const bool attached = fgThread && fgThread != cur && AttachThreadInput(cur, fgThread, TRUE);
    SetForegroundWindow(fg);
    keybd_event(VK_CONTROL, 0, 0, 0);
    keybd_event('V', 0, 0, 0);
    keybd_event('V', 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
    if (attached)
        AttachThreadInput(cur, fgThread, FALSE);
    m_prevFg = 0;
    return true;
}

void SearchWindow::activateRow(int row)
{
    if (row < 0 || row >= m_list->count())
        return;
    const int idx = m_list->item(row)->data(Qt::UserRole).toInt();
    if (idx < 0 || idx >= m_results.size())
        return;
    const SearchResult r = m_results[idx];

    switch (r.kind) {
    case SearchResult::App: {
        // 键和排序那边同一口径：没有 .lnk 的应用用 target 记，别挤到同一个空键上
        bumpUsage(r.extra.isEmpty() ? r.target : r.extra);
        saveUsage();
        hide();   // 先收窗再启动：带 DDE 段的 .lnk 同步执行要等握手回调（实测 729ms）
        // 优先通过 lnk 启动（保留参数/工作目录）；异步拿不到失败回执，所以只在没有 lnk 时退到目标
        Utils::launch(r.extra.isEmpty() ? r.target : r.extra, {}, {}, true);
        break;
    }
    case SearchResult::File:
    case SearchResult::Web: {   // ShellExecute 会走默认程序/默认浏览器
        // 常用面板要按"真的打开过"排序，所以文件和书签也记使用次数（"用搜索引擎搜索…"不算书签）
        if (r.kind == SearchResult::File)
            bumpUsage(QStringLiteral("file:") + r.target);
        else if (!r.title.startsWith(QStringLiteral("用搜索引擎搜索")))
            bumpUsage(QStringLiteral("web:") + r.title);
        saveUsage();
        hide();
        Utils::launch(r.target, {}, {}, true);
        break;
    }
    case SearchResult::Command: {
        if (r.target == "rescan") {
            m_hint->setText("正在重新扫描，完成后会自动刷新…");
            startScan(false, true);
        } else if (r.target.startsWith(QLatin1String("tool:"))) {
            // 工具就是 DLL 插件：先弹设置页，再展开那张卡；r.extra 有内容就顺手递给那页
            hide();
            openTool(r.target.mid(5), r.extra);
        } else if (r.target.startsWith(QLatin1String("clip:"))) {
            const QString arg = r.target.mid(5);
            if (arg == QLatin1String("on")) {
                ClipboardStore::instance().setEnabled(true);
                m_hint->setText(QStringLiteral("已开始记录剪贴板（设置 → 配置 里可以关掉）"));
                runSearch();
            } else if (arg == QLatin1String("clear")) {
                ClipboardStore::instance().clear();
                m_hint->setText(QStringLiteral("剪贴板历史已清空"));
                runSearch();
            } else if (arg != QLatin1String("none")) {
                ClipboardStore::instance().restore(arg.toInt());
                hide();
            }
        }
        break;
    }
    case SearchResult::PluginEntry: {
        m_input->setText("init " + r.target + " ");
        m_input->setFocus();
        break;
    }
    case SearchResult::PluginItem: {
        // r.target = 插件 id, r.extra = 组件 label
        const QString key = r.target + "|" + r.extra;
        DevItem it = m_detectItems.value(key);
        if (it.ok) {
            m_detecting.remove(r.target);
            m_detectItems.remove(key);
            runSearch();
        } else if (!it.wingetId.isEmpty()) {
            QString err;
            DevEnv::runInstall(this, it, &err);
            m_detectItems.remove(key);
            m_hint->setText("安装完成或取消后，按 Enter 重新检测");
            runSearch();
        } else if (!it.note.isEmpty()) {
            m_hint->setText(it.note);
        }
        break;
    }
    case SearchResult::RegIssue: {
        const int issueIdx = r.target.toInt();
        if (issueIdx < 0 || issueIdx >= m_regIssues.size())
            break;
        QString err;
        if (RegClean::deleteIssue(m_regIssues[issueIdx], &err)) {
            m_hint->setText("已删除并备份。重新扫描中…");
        } else {
            m_hint->setText(err);
        }
        runSearch();
        break;
    }
    case SearchResult::Info:
        break;
    }
}

// ---------- init：开发环境 ----------

void SearchWindow::handleInit(const QString& arg)
{
    const QVector<DevPlugin> plugins = DevEnv::loadPlugins();

    if (arg.isEmpty()) {
        addSection("── 开发环境插件 ──");
        for (const DevPlugin& p : plugins) {
            SearchResult r;
            r.kind = SearchResult::PluginEntry;
            r.title = p.name;
            r.subtitle = QString("init %1 · %2 个组件 · Enter 查看")
                             .arg(p.id).arg(p.items.size());
            r.target = p.id;
            r.icon = letterIcon(p.name, colorForKind(p.id));
            addItem(r);
        }
        SearchResult tip;
        tip.kind = SearchResult::Info;
        tip.actionable = false;
        tip.title = "插件定义在 %APPDATA%\\ltool\\plugins，可自行添加";
        addItem(tip);
        return;
    }

    // 按 id 精确/前缀匹配插件
    const DevPlugin* found = nullptr;
    for (const DevPlugin& p : plugins)
        if (p.id.compare(arg, Qt::CaseInsensitive) == 0)
            found = &p;
    if (!found) {
        QVector<const DevPlugin*> pref;
        for (const DevPlugin& p : plugins)
            if (p.id.startsWith(arg, Qt::CaseInsensitive))
                pref.append(&p);
        if (pref.size() == 1)
            found = pref.first();
    }
    if (!found) {
        QStringList ids;
        for (const DevPlugin& p : plugins)
            ids.append(p.id);
        SearchResult r;
        r.kind = SearchResult::Info;
        r.actionable = false;
        r.title = QString("未找到插件：%1").arg(arg);
        r.subtitle = ids.isEmpty() ? QString() : "可用插件：" + ids.join(", ");
        addItem(r);
        return;
    }

    DevPlugin p = *found;
    addSection(QString("── %1 (init %2) ──").arg(p.name, p.id));

    // 检查已有检测结果的覆盖情况：全部有结果 → 展示；部分缺失 → 展示+补检
    int cachedCount = 0;
    for (const DevItem& it : p.items) {
        if (m_detectItems.contains(p.id + "|" + it.label))
            ++cachedCount;
    }
    const bool allCached = cachedCount == p.items.size();
    const bool someCached = cachedCount > 0;

    for (const DevItem& it : p.items) {
        const QString key = p.id + "|" + it.label;
        SearchResult r;
        if (m_detectItems.contains(key)) {
            const DevItem det = m_detectItems.value(key);
            r.kind = SearchResult::PluginItem;
            r.title = it.label;
            r.target = p.id;
            r.extra = it.label;
            r.icon = letterIcon(det.ok ? "✓" : "!",
                                det.ok ? QColor(46, 160, 88) : QColor(214, 128, 15));
            if (det.ok) {
                r.subtitle = "已安装 · " + det.version;
            } else if (!det.wingetId.isEmpty()) {
                r.subtitle = QString("未安装 · Enter 用 winget 安装 (%1)").arg(det.wingetId);
            } else {
                r.subtitle = det.note.isEmpty() ? "未检测到" : det.note;
            }
        } else {
            r.kind = SearchResult::Info;
            r.actionable = false;
            r.title = it.label;
            r.subtitle = someCached ? "检测中…" : "检测中…";
        }
        addItem(r);
    }
    if (allCached) {
        SearchResult tip;
        tip.kind = SearchResult::Info;
        tip.actionable = false;
        tip.title = "Enter：已安装项重新检测 / 缺失项安装";
        addItem(tip);
        return;
    }
    if (m_detecting.contains(p.id))
        return;
    m_detecting.insert(p.id);

    const int run = ++m_detectRun;
    const QString id = p.id;
    auto future = QtConcurrent::run([p]() mutable -> QHash<QString, DevItem> {
        DevEnv::detect(&p);
        QHash<QString, DevItem> map;
        for (const DevItem& it : p.items)
            map.insert(p.id + "|" + it.label, it);
        return map;
    });
    auto* watcher = new QFutureWatcher<QHash<QString, DevItem>>(this);
    connect(watcher, &QFutureWatcher<QHash<QString, DevItem>>::finished, this,
            [this, watcher, run, id] {
                watcher->deleteLater();
                if (run != m_detectRun)
                    return;
                m_detecting.remove(id);
                const QHash<QString, DevItem> res = watcher->result();
                for (auto it = res.begin(); it != res.end(); ++it)
                    m_detectItems.insert(it.key(), it.value());
                const QString cur = m_input->text();
                if (cur.section(' ', 0, 0).compare("init", Qt::CaseInsensitive) == 0)
                    runSearch();
            });
    watcher->setFuture(future);
}

// ---------- path：环境变量 ----------

void SearchWindow::handlePath(const QString& rest)
{
    const QString t = rest.trimmed();
    if (t.startsWith("add", Qt::CaseInsensitive)) {
        QString dir = t.mid(3).trimmed();
        dir.remove('"');
        if (dir.isEmpty()) {
            SearchResult r;
            r.kind = SearchResult::Info;
            r.actionable = false;
            r.title = "用法：path add <目录>";
            addItem(r);
            return;
        }
        QString err;
        const bool ok = EnvPath::addToUserPath(dir, &err);
        SearchResult r;
        r.kind = SearchResult::Info;
        r.actionable = false;
        r.title = ok ? QString("已加入用户 PATH：%1").arg(QDir::toNativeSeparators(dir))
                     : err;
        r.subtitle = ok ? "对新开的终端/程序生效（已广播 WM_SETTINGCHANGE）" : QString();
        addItem(r);
        return;
    }
    if (t.startsWith("del", Qt::CaseInsensitive)) {
        QString dir = t.mid(3).trimmed();
        dir.remove('"');
        QString err;
        const bool ok = EnvPath::removeFromUserPath(dir, &err);
        SearchResult r;
        r.kind = SearchResult::Info;
        r.actionable = false;
        r.title = ok ? QString("已从 PATH 移除：%1").arg(dir) : err;
        addItem(r);
        return;
    }

    // path / path list：列出
    const QStringList entries = EnvPath::userPathEntries();
    addSection(QString("── 用户 PATH（共 %1 项）──").arg(entries.size()));
    for (const QString& e : entries) {
        SearchResult r;
        r.kind = SearchResult::Info;
        r.actionable = false;
        r.title = Utils::expandEnv(e);
        r.subtitle = e == r.title ? QString() : e;
        addItem(r);
    }
    SearchResult tip;
    tip.kind = SearchResult::Info;
    tip.actionable = false;
    tip.title = "path add <目录> 添加 · path del <目录> 移除 · 应用结果右键也可加入 PATH";
    addItem(tip);
}

// ---------- reg：注册表整理 ----------

void SearchWindow::handleReg(const QString& rest)
{
    const QString t = rest.trimmed();

    if (t.startsWith("clean", Qt::CaseInsensitive)) {
        // 命令行里只自动删"安全"级（启动项/缓存/App Paths/遗留键）；
        // 卸载记录、服务、文件关联、COM 这些要人去分析页看着明细勾，别一句话盲删上百个键
        QVector<RegIssue> all = RegClean::quickScan();
        QVector<RegIssue> issues;
        int held = 0;
        for (const RegIssue& it : all) {
            if (it.deletable() && it.risk == RegIssue::Risk::Safe)
                issues.append(it);
            else if (it.deletable())
                ++held;
        }
        if (issues.isEmpty()) {
            SearchResult r;
            r.kind = SearchResult::Info;
            r.actionable = false;
            r.title = held > 0
                ? QString("没有可以自动删的项（另有 %1 项属于谨慎级，去 工具 → 注册表分析 里看明细）").arg(held)
                : QString("注册表很干净，没有发现残留项");
            addItem(r);
            return;
        }
        int okCount = 0;
        QStringList fails;
        for (const RegIssue& it : issues) {
            QString err;
            if (RegClean::deleteIssue(it, &err))
                ++okCount;
            else
                fails.append(QString("%1：(%2)").arg(it.title, err));
        }
        SearchResult r;
        r.kind = SearchResult::Info;
        r.actionable = false;
        r.title = QString("已清理 %1/%2 项（每项删除前已自动备份 .reg）")
                      .arg(okCount).arg(issues.size());
        r.subtitle = QString("备份目录：%1%2")
                         .arg(QDir::toNativeSeparators(Utils::backupsDir()),
                              held > 0
                                  ? QString(" · 另有 %1 项谨慎级没动，去 工具 → 注册表分析 里勾").arg(held)
                                  : QString());
        addItem(r);
        for (const QString& f : fails) {
            SearchResult fr;
            fr.kind = SearchResult::Info;
            fr.actionable = false;
            fr.title = f;
            addItem(fr);
        }
        return;
    }

    // reg / reg scan：列出问题。搜索框里只做快的那几类，
    // COM 组件要遍历上万个键，留给「工具 → 注册表分析」那页去跑
    const QVector<RegIssue> issues = RegClean::quickScan();
    addSection("── 注册表体检 ──");
    if (issues.isEmpty()) {
        SearchResult r;
        r.kind = SearchResult::Info;
        r.actionable = false;
        r.title = "未发现无效项（卸载残留 / 启动项失效 / 缓存残留）";
        addItem(r);
        return;
    }
    SearchResult head;
    head.kind = SearchResult::Info;
    head.actionable = false;
    head.title = QString("发现 %1 个无效项 · Enter 删除单项（自动备份）· reg clean 全部清理")
                     .arg(issues.size());
    addItem(head);

    m_regIssues = issues;
    for (int i = 0; i < issues.size(); ++i) {
        const RegIssue& it = issues[i];
        SearchResult r;
        r.kind = SearchResult::RegIssue;
        r.title = it.title;
        r.subtitle = QString("[%1] %2").arg(RegClean::categoryLabel(it), it.detail);
        r.target = QString::number(i);
        r.icon = letterIcon("C", QColor(214, 69, 69));
        addItem(r);
    }
}

// ---------- 扫描 ----------

void SearchWindow::startScan(bool quiet, bool forceDeep)
{
    if (!m_scanning.testAndSetRelaxed(0, 1))
        return;
    auto future = QtConcurrent::run([]() -> QVector<AppEntry> {
        const auto list = AppScanner::scan();
        AppScanner::saveIndex(list);
        return list;
    });
    auto* watcher = new QFutureWatcher<QVector<AppEntry>>(this);
    connect(watcher, &QFutureWatcher<QVector<AppEntry>>::finished, this, [this, watcher, quiet] {
        m_apps = watcher->result();
        m_apps += SysTools::systemTools();
        watcher->deleteLater();
        m_scanning.storeRelaxed(0);
        Utils::log(QString("scan finished: %1 apps").arg(m_apps.size()));
        watchAppDirs();     // 安装器常新建子目录，扫完补挂一次监视
        if (!quiet) {
            m_hint->setText(QString("扫描完成：应用 %1 · 文件索引进行中…").arg(m_apps.size()));
            runSearch();
        }
    });
    watcher->setFuture(future);
    FileIndexer::instance().reindexAsync(forceDeep);
    if (!quiet)
        m_hint->setText("正在扫描桌面与开始菜单…");
}

// ---------- 使用统计 ----------

void SearchWindow::loadUsage()
{
    QFile f(Utils::dataDir() + "/usage.json");
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = o.begin(); it != o.end(); ++it) {
        // 那个空键是旧版本的记账口径留下的（所有没有 .lnk 的应用共用 ""，这台机器涨到 43 次），
        // 已经没法归属到具体哪个应用，读的时候丢掉 —— 留着它，QQEX 这种就会一直压在 QQ 前面。
        if (it.key().isEmpty())
            continue;
        m_usage.insert(it.key(), it.value().toInt());
    }
}

void SearchWindow::saveUsage()
{
    QJsonObject o;
    for (auto it = m_usage.begin(); it != m_usage.end(); ++it)
        o.insert(it.key(), it.value());
    QSaveFile f(Utils::dataDir() + "/usage.json");
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

void SearchWindow::bumpUsage(const QString& lnkPath)
{
    m_usage[lnkPath] = m_usage.value(lnkPath, 0) + 1;
}

// ---------- 右键菜单 ----------

void SearchWindow::openContextMenu(const QPoint& globalPos)
{
    const int row = m_list->currentRow();
    if (row < 0)
        return;
    const int idx = m_list->item(row)->data(Qt::UserRole).toInt();
    if (idx < 0 || idx >= m_results.size())
        return;
    const SearchResult& r = m_results[idx];

    m_menu = new QMenu(this);
    m_menu->setAttribute(Qt::WA_DeleteOnClose);
    connect(m_menu, &QMenu::aboutToHide, this, [this] {
        m_menu = nullptr;
        if (!isActiveWindow())
            hide();
    });

    const QString mainFile = (r.kind == SearchResult::App)
        ? (r.target.isEmpty() ? r.extra : r.target) : r.target;
    if ((r.kind == SearchResult::App || r.kind == SearchResult::File)
        && QFileInfo(mainFile).isFile()) {
        m_menu->addSeparator();
        m_menu->addAction("解除占用", this, [this, mainFile] {
            const auto locks = FileTools::findLocks(mainFile);
            if (locks.isEmpty()) {
                QMessageBox::information(this, "ltool", "没有发现占用该文件的进程。");
                return;
            }
            QStringList names;
            for (const auto& l : locks)
                names.append(QString("%1 (pid %2)").arg(l.name).arg(l.pid));
            if (QMessageBox::question(this, "解除占用",
                    QString("以下进程正在占用：\n%1\n结束它们？").arg(names.join("\n")))
                == QMessageBox::Yes) {
                const QString msg = FileTools::unlock(mainFile);
                msg.isEmpty()
                    ? QMessageBox::information(this, "ltool", "已全部结束。")
                    : QMessageBox::warning(this, "ltool", msg);
            }
        });
        m_menu->addAction("文件粉碎", this, [this, mainFile] {
            if (QMessageBox::warning(this, "文件粉碎",
                    QString("将对文件进行三轮覆写并永久删除（不可恢复）：\n%1\n确定？")
                        .arg(QDir::toNativeSeparators(mainFile)),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
                return;
            const QString err = FileTools::shred(mainFile);
            err.isEmpty()
                ? QMessageBox::information(this, "ltool", "文件已粉碎。")
                : QMessageBox::warning(this, "ltool", err);
        });
    }

    if (r.kind == SearchResult::App) {
        const QString where = r.extra;
        m_menu->addAction("打开所在位置", this, [where] { Utils::revealInExplorer(where); });
        if (!r.target.isEmpty()) {
            const QString dir = QFileInfo(r.target).absolutePath();
            m_menu->addAction("把程序目录加入 PATH", this, [this, dir] {
                QString err;
                const bool ok = EnvPath::addToUserPath(dir, &err);
                m_hint->setText(ok ? QString("已加入 PATH：%1").arg(dir) : err);
            });
        }
        m_menu->addAction("复制路径", this, [where] {
            QGuiApplication::clipboard()->setText(QDir::toNativeSeparators(where));
        });
    } else if (r.kind == SearchResult::File) {
        m_menu->addAction("打开所在文件夹", this, [t = r.target] {
            Utils::revealInExplorer(t);
        });
        m_menu->addAction("复制路径", this, [t = r.target] {
            QGuiApplication::clipboard()->setText(QDir::toNativeSeparators(t));
        });
    }
    if (m_menu->isEmpty())
        m_menu->addAction("(无可用操作)")->setEnabled(false);
    m_menu->popup(globalPos);
}
