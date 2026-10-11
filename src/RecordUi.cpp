#include "RecordUi.h"

#include "Recorder.h"
#include "Utils.h"

#include <windows.h>
#include <dwmapi.h>

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

namespace {

struct WinRow {
    WId hwnd = 0;
    QString title;
    QString exe;
    int w = 0;
    int h = 0;
};

// 桌面上"看得见、有标题、够大"的顶层窗口。被虚拟桌面收起（cloaked）的不算，
// 因为那种窗口 WGC 也不会给帧。
QVector<WinRow> visibleWindows(HWND skip1, HWND skip2)
{
    QVector<WinRow> out;
    struct Ctx { QVector<WinRow>* v; HWND s1; HWND s2; } ctx { &out, skip1, skip2 };
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        if (!IsWindowVisible(h))
            return TRUE;
        DWORD cloaked = 0;
        DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        if (cloaked || h == c->s1 || h == c->s2)
            return TRUE;
        RECT rc {};
        if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &rc, sizeof(rc))))
            GetWindowRect(h, &rc);
        const int w = rc.right - rc.left, hh = rc.bottom - rc.top;
        if (w < 80 || hh < 60)
            return TRUE;
        WCHAR title[512] = {};
        GetWindowTextW(h, title, 512);
        if (!title[0])
            return TRUE;
        WinRow r;
        r.hwnd = reinterpret_cast<WId>(h);
        r.title = QString::fromWCharArray(title);
        r.w = w;
        r.h = hh;
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
            WCHAR exe[512] = {};
            DWORD n = 512;
            if (QueryFullProcessImageNameW(p, 0, exe, &n))
                r.exe = QFileInfo(QString::fromWCharArray(exe, int(n))).fileName();
            CloseHandle(p);
        }
        c->v->push_back(r);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return out;
}

Recorder* g_rec = nullptr;
RecordBar* g_bar = nullptr;
WindowPicker* g_picker = nullptr;

// 主显示器那块屏的 HMONITOR + 物理像素尺寸（选择器顶上那条"整个屏幕"就用它）。
// 主屏一定包含虚拟桌面原点，所以不用比屏幕名字（Qt 的名字和 GetMonitorInfo 的没有可靠对应）。
struct ScreenRow { WId mon = 0; int w = 0; int h = 0; };
ScreenRow primaryMonitor()
{
    ScreenRow r;
    const POINT origin { 0, 0 };
    if (HMONITOR m = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY)) {
        MONITORINFO mi {};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(m, &mi)) {
            r.mon = reinterpret_cast<WId>(m);
            r.w = mi.rcMonitor.right - mi.rcMonitor.left;
            r.h = mi.rcMonitor.bottom - mi.rcMonitor.top;
        }
    }
    return r;
}

void beginRecording(WId target, const QString& title, bool monitor)
{
    QDir().mkpath(Utils::recordingsDir());
    const QString path = QString("%1/rec-%2.mp4")
                             .arg(Utils::recordingsDir(),
                                  QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
    if (!g_rec)
        g_rec = new Recorder;
    QSettings st(Utils::dataDir() + "/config.ini", QSettings::IniFormat);
    Recorder::Options o;
    o.fps = qBound(5, st.value(QStringLiteral("ui/recFps"), 15).toInt(), 60);
    o.videoBitrateKbps = qBound(1000, st.value(QStringLiteral("ui/recBitrateKbps"), 8000).toInt(),
                                40000);
    QString err;
    if (!g_rec->start(target, o, path, &err, monitor)) {
        Utils::log(QString("record start failed: %1").arg(err));
        QMessageBox::warning(nullptr, QStringLiteral("录不了"),
                             err.isEmpty() ? QStringLiteral("不知道哪里不对，反正没录起来") : err);
        return;
    }
    if (g_bar) {
        g_bar->close();
        g_bar = nullptr;
    }
    g_bar = new RecordBar(g_rec, path);
    g_bar->show();
    int rw = 0, rh = 0;
    if (monitor) {
        MONITORINFO mi {};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(reinterpret_cast<HMONITOR>(target), &mi)) {
            rw = mi.rcMonitor.right - mi.rcMonitor.left;
            rh = mi.rcMonitor.bottom - mi.rcMonitor.top;
        }
    } else {
        RECT rc {};
        if (GetWindowRect(reinterpret_cast<HWND>(target), &rc)) {
            rw = rc.right - rc.left;
            rh = rc.bottom - rc.top;
        }
    }
    Utils::log(QString("record start: %1 title=%2 kind=%3 fps=%4 kbps=%5 画幅=%6x%7")
                   .arg(path, title, monitor ? QStringLiteral("屏幕") : QStringLiteral("窗口"))
                   .arg(o.fps).arg(o.videoBitrateKbps).arg(rw).arg(rh));
}

} // namespace

// ---------------- 窗口选择 ----------------

WindowPicker::WindowPicker(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_DeleteOnClose, false);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 8, 10, 8);
    lay->setSpacing(6);
    auto* title = new QLabel(QStringLiteral("录什么？（整块屏幕 / 某一个窗口）"), this);
    title->setStyleSheet("color:#e8ebf2;font-size:13px;font-weight:600;");
    auto* filter = new QLineEdit(this);
    filter->setPlaceholderText(QStringLiteral("输入关键字过滤（输「屏」就剩整块屏幕），回车开始录，Esc 取消"));
    auto* list = new QListWidget(this);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    lay->addWidget(title);
    lay->addWidget(filter);
    lay->addWidget(list, 1);
    list->setObjectName(QStringLiteral("list"));
    filter->setObjectName(QStringLiteral("filter"));
    setStyleSheet("QWidget{background:#26272c;border:1px solid #3a3c45;}"
                  "QLineEdit,QListWidget{background:#1b1c20;color:#e8ebf2;border:1px solid #3a3c45;"
                  "border-radius:5px;padding:4px 6px;}"
                  "QListWidget::item{padding:5px 6px;border-radius:4px;}"
                  "QListWidget::item:selected{background:#3f6ae0;}"
                  "QLabel{border:none;}");
    resize(460, 360);

    const auto rebuild = [this, list, filter] {
        list->clear();
        // 整块屏幕排在最上面一条：录屏的人十次有九次要的是"就录我看到的这一整块"。
        // UserRole+2 是"这条是屏幕不是窗口"的标志，没设的（窗口那些行）toBool() 就是 false。
        const ScreenRow scr = primaryMonitor();
        if (scr.mon) {
            auto* it = new QListWidgetItem(QStringLiteral("整个屏幕 · 主显示器 · %1x%2")
                                               .arg(scr.w).arg(scr.h), list);
            it->setData(Qt::UserRole, qlonglong(scr.mon));
            it->setData(Qt::UserRole + 1, QStringLiteral("整个屏幕"));
            it->setData(Qt::UserRole + 2, true);
        }
        const HWND skip = reinterpret_cast<HWND>(winId());
        for (const WinRow& r : visibleWindows(skip, g_bar ? reinterpret_cast<HWND>(g_bar->winId()) : nullptr)) {
            auto* it = new QListWidgetItem(QStringLiteral("%1  ·  %2  ·  %3x%4")
                                               .arg(r.title.left(46), r.exe).arg(r.w).arg(r.h), list);
            it->setData(Qt::UserRole, qlonglong(r.hwnd));
            it->setData(Qt::UserRole + 1, r.title);
        }
        if (list->count())
            list->setCurrentRow(0);
    };
    rebuild();
    m_list = list;

    connect(filter, &QLineEdit::textChanged, this, [list, filter] {
        const QString needle = filter->text().trimmed();
        for (int i = 0; i < list->count(); ++i)
            list->item(i)->setHidden(!needle.isEmpty() && !list->item(i)->text().contains(needle, Qt::CaseInsensitive));
    });
    const auto choose = [this, list] {
        auto* it = list->currentItem();
        if (!it)
            return;
        emit picked(WId(it->data(Qt::UserRole).toLongLong()), it->data(Qt::UserRole + 1).toString(),
                    it->data(Qt::UserRole + 2).toBool());
    };
    connect(list, &QListWidget::itemActivated, this, [choose](QListWidgetItem*) { choose(); });
    connect(filter, &QLineEdit::returnPressed, this, [this, choose] {
        // 回车先从可见项里挑第一个（"输入 Qoder 回车"就是录 Qoder）
        auto* list = qobject_cast<QListWidget*>(m_list);
        for (int i = 0; i < list->count(); ++i) {
            if (!list->item(i)->isHidden()) {
                list->setCurrentRow(i);
                break;
            }
        }
        choose();
    });

    // Esc 要能从输入框和列表里也收得到：QLineEdit 会把 Esc 吃掉（它有自己的 escapePressed），
    // 所以除了 keyPressEvent，还得给这两个子控件装事件过滤器。
    filter->installEventFilter(this);
    list->installEventFilter(this);
}

void WindowPicker::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        hide();
        return;
    }
    QWidget::keyPressEvent(e);
}

bool WindowPicker::eventFilter(QObject* watched, QEvent* e)
{
    if (e->type() == QEvent::KeyPress
        && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) {
        hide();
        return true;
    }
    if (e->type() == QEvent::WindowDeactivate) {
        // 点到别处 = 不录了。这个窗是 activateWindow 过的，失焦就是用户真的走开了；
        // 不收起的话它会一直压在要录的那个窗口上面。
        hide();
        return false;
    }
    return QWidget::eventFilter(watched, e);
}

// ---------------- 悬浮条 ----------------

RecordBar::RecordBar(Recorder* rec, const QString& path, QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
      m_rec(rec), m_path(path)
{
    // 别抢前台：他正要点的是被录的那个窗口
    setAttribute(Qt::WA_ShowWithoutActivating);
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(10, 4, 8, 4);
    lay->setSpacing(8);
    auto* dot = new QLabel(QStringLiteral("●"), this);
    dot->setStyleSheet("color:#e04a4a;border:none;font-size:13px;");
    m_text = new QLabel(QStringLiteral("正在录…"), this);
    m_text->setObjectName(QStringLiteral("text"));
    m_stop = new QPushButton(QStringLiteral("停止"), this);
    m_stop->setObjectName(QStringLiteral("stop"));
    m_stop->setCursor(Qt::PointingHandCursor);
    m_cancel = new QPushButton(QStringLiteral("不要了"), this);
    m_cancel->setObjectName(QStringLiteral("cancel"));
    m_cancel->setCursor(Qt::PointingHandCursor);
    m_cancel->setToolTip(QStringLiteral("停下这一局并且把已经录的丢掉（文件删掉，不留在录像目录里）"));
    lay->addWidget(dot);
    lay->addWidget(m_text);
    lay->addStretch(1);
    lay->addWidget(m_cancel);
    lay->addWidget(m_stop);
    setStyleSheet("QWidget{background:#26272c;border:1px solid #4a4d57;border-radius:8px;}"
                  "QLabel{color:#e8ebf2;border:none;font-size:12px;}"
                  "QPushButton{background:#3a3d46;color:#e8ebf2;border:none;border-radius:5px;"
                  "padding:3px 10px;}"
                  "QPushButton:hover{background:#4a4f5c;}");
    setAttribute(Qt::WA_TranslucentBackground, false);
    adjustSize();
    setFixedWidth(320);

    const QRect a = screen()->availableGeometry();
    move(a.right() - width() - 16, a.bottom() - height() - 12);

    connect(m_stop, &QPushButton::clicked, this, [this] {
        m_stop->setEnabled(false);
        m_cancel->setEnabled(false);
        m_text->setText(QStringLiteral("正在收尾（编码要把最后一帧写完）…"));
        QCoreApplication::processEvents();
        m_rec->stop();
        finish(m_rec->errorText());
    });
    // 「不要了」= 停下这一局并且把文件删掉。Recorder 没有"只停不写盘"这条路
    // （MP4 是收尾才落盘的），所以照常收尾，然后把那个废文件原地删了 ——
    // 录到一半的东西留在录像目录里就是垃圾，还得人去删。
    connect(m_cancel, &QPushButton::clicked, this, [this] {
        m_stop->setEnabled(false);
        m_cancel->setEnabled(false);
        m_text->setText(QStringLiteral("正在收尾（要把这一局丢掉）…"));
        QCoreApplication::processEvents();
        m_discard = true;
        m_rec->stop();
        finish(m_rec->errorText());
    });

    auto* tick = new QTimer(this);
    connect(tick, &QTimer::timeout, this, &RecordBar::poll);
    tick->start(200);
}

void RecordBar::poll()
{
    if (m_rec->isDone()) {              // 窗口关了/改尺寸了：worker 自己收的尾
        if (!m_finished)
            finish(m_rec->errorText());
        return;
    }
    const qint64 ms = m_rec->elapsedMs();
    // 不报"已写多少 KB"：MP4 是收尾才落盘的，录的过程中一直是 0，写上去就是撒谎
    m_text->setText(QStringLiteral("⏺ %1:%2 · %3 帧")
                        .arg(ms / 60000)
                        .arg((ms / 1000) % 60, 2, 10, QLatin1Char('0'))
                        .arg(m_rec->framesCaptured()));
}

void RecordBar::finish(const QString& error)
{
    m_finished = true;   // 只收一次尾：以前这里每 200ms 重进一遍，日志被刷爆
    if (m_discard) {
        QFile::remove(m_path);
        m_text->setText(QStringLiteral("已取消，没保存（丢掉 %1 帧）").arg(m_rec->framesCaptured()));
        m_stop->setText(QStringLiteral("关闭"));
        m_cancel->setVisible(false);
        Utils::log(QString("record cancelled: %1 frames=%2 文件已删")
                       .arg(m_path).arg(m_rec->framesCaptured()));
        m_stop->setEnabled(true);
        disconnect(m_stop, nullptr, this, nullptr);
        connect(m_stop, &QPushButton::clicked, this, [this] { close(); });
        auto* t = new QTimer(this);
        connect(t, &QTimer::timeout, this, &QWidget::close);
        t->setSingleShot(true);
        t->start(8000);
        return;
    }
    if (!error.isEmpty()) {
        m_text->setText(QStringLiteral("没录成：%1").arg(error));
        m_stop->setText(QStringLiteral("知道了"));
        Utils::log(QString("record failed: %1 path=%2").arg(error, m_path));
    } else {
        const qint64 bytes = m_rec->fileBytes();
        // 13KB 显示成 "0.0 MB" 就是没说实话
        const QString size = bytes >= 1024 * 1024
            ? QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1)
            : QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 0);
        m_text->setText(QStringLiteral("已保存 %1 帧 / %2 → %3")
                            .arg(m_rec->framesCaptured())
                            .arg(size, QFileInfo(m_path).fileName()));
        m_stop->setText(QStringLiteral("打开所在目录"));
        Utils::log(QString("record saved: %1 frames=%2 bytes=%3")
                       .arg(m_path).arg(m_rec->framesCaptured()).arg(bytes));
    }
    m_stop->setEnabled(true);
    disconnect(m_stop, nullptr, this, nullptr);
    connect(m_stop, &QPushButton::clicked, this, [this, error] {
        if (error.isEmpty())
            Utils::revealInExplorer(m_path);
        close();
    });
    auto* t = new QTimer(this);
    connect(t, &QTimer::timeout, this, &QWidget::close);
    t->setSingleShot(true);
    t->start(error.isEmpty() ? 30000 : 12000);   // 结果留在条上，不用弹窗打断
}

void RecordBar::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        m_dragging = true;
        m_press = e->globalPosition().toPoint() - geometry().topLeft();
    }
}

void RecordBar::mouseMoveEvent(QMouseEvent* e)
{
    if (m_dragging && (e->buttons() & Qt::LeftButton))
        move(e->globalPosition().toPoint() - m_press);
}

void RecordBar::mouseReleaseEvent(QMouseEvent*)
{
    m_dragging = false;
}

// ---------------- 入口 ----------------

namespace RecordUi {

void startViaPicker()
{
    if (isRecording()) {
        if (g_bar)
            g_bar->raise();
        return;
    }
    if (!g_rec)
        g_rec = new Recorder;
    QString why;
    if (!Recorder::available(&why)) {
        QMessageBox::warning(nullptr, QStringLiteral("录屏"), why);
        return;
    }
    // 选择窗开着的时候再按一次 F2 = 取消（它没有标题栏、没有关闭按钮，
    // 光靠 Esc 和"点到别处"还不够：手就放在热键上，再按一次最自然）
    if (g_picker && g_picker->isVisible()) {
        g_picker->hide();
        return;
    }
    // 每次都要重新数一遍窗口（他可能刚开/刚关了几个）
    if (g_picker) {
        g_picker->hide();
        delete g_picker;
        g_picker = nullptr;
    }
    g_picker = new WindowPicker;
    QObject::connect(g_picker, &WindowPicker::picked,
                     [](WId target, const QString& title, bool monitor) {
        if (g_picker)
            g_picker->hide();
        beginRecording(target, title, monitor);
    });
    const QPoint cur = QCursor::pos();
    const QRect a = QGuiApplication::screenAt(cur)
                        ? QGuiApplication::screenAt(cur)->availableGeometry()
                        : QApplication::primaryScreen()->availableGeometry();
    g_picker->move(qMin(cur.x() + 12, a.right() - g_picker->width() - 8),
                   qMin(cur.y() + 12, a.bottom() - g_picker->height() - 8));
    g_picker->show();
    g_picker->raise();
    g_picker->activateWindow();
}

bool isRecording()
{
    return g_rec && g_rec->running();
}

} // namespace RecordUi
