#include "DiskPage.h"
#include "Utils.h"

#include <QCheckBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFrame>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTabBar>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>

#include <windows.h>
#include <shellapi.h>

namespace {

struct CleanItem {
    const char* name;
    QString path;
    bool admin;
};

QVector<CleanItem> cleanItems()
{
    const QString local = QDir::homePath() + "/AppData/Local";
    return {
        {"用户临时文件", QDir::tempPath(), false},
        {"系统临时文件", "C:/Windows/Temp", true},
        {"Windows 更新缓存", "C:/Windows/SoftwareDistribution/Download", true},
        {"DirectX 着色器缓存", local + "/D3DSCache", false},
        {"CrashDumps 崩溃转储", local + "/CrashDumps", false},
        {"pip 缓存", local + "/pip/cache", false},
        {"npm 缓存", local + "/npm-cache", false},
        {"Chrome 缓存", local + "/Google/Chrome/User Data/Default/Cache", false},
        {"Edge 缓存", local + "/Microsoft/Edge/User Data/Default/Cache", false},
    };
}

// 必须带 Hidden|System：QDirIterator 默认不返回隐藏/系统条目，少了它们
// pagefile、回收站、系统目录里的东西整个漏掉（实测 C 盘少算约一半容量）
qint64 dirSize(const QString& path)
{
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::NoSymLinks | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

QString human(qint64 bytes)
{
    if (bytes < 0)
        return QStringLiteral("不存在");
    if (bytes < 1024)
        return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024LL * 1024 * 1024)
        return QString("%1 MB").arg(bytes / 1024.0 / 1024, 0, 'f', 1);
    return QString("%1 GB").arg(bytes / 1024.0 / 1024 / 1024, 0, 'f', 2);
}

qint64 clearDir(const QString& path, QString* err)
{
    qint64 freed = 0;
    QDir d(path);
    if (!d.exists())
        return 0;
    const auto entries = d.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot
                                         | QDir::NoSymLinks | QDir::System | QDir::Hidden);
    for (const QFileInfo& fi : entries) {
        const qint64 sz = fi.isDir() ? dirSize(fi.absoluteFilePath()) : fi.size();
        bool ok = false;
        if (fi.isDir())
            ok = QDir(fi.absoluteFilePath()).removeRecursively();
        else
            ok = QFile::remove(fi.absoluteFilePath());
        if (ok)
            freed += sz;
        else if (err && err->size() < 200)
            *err += QString("  跳过 %1（占用中/无权限）\n").arg(fi.fileName());
    }
    return freed;
}

// 满宽占比条：轨道铺满可用宽度，填充按同级最大值归一
class TrackBar : public QFrame {
public:
    explicit TrackBar(QWidget* parent = nullptr) : QFrame(parent)
    {
        setFixedHeight(8);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setRatio(qreal r, bool hottest)
    {
        m_ratio = r;
        m_hottest = hottest;
        update();
    }

    QSize minimumSizeHint() const override { return QSize(24, 8); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#2b2d35"));
        p.drawRoundedRect(QRectF(0, 1, width(), 6), 3, 3);
        if (m_ratio <= 0.0)
            return;
        const qreal w = qMax<qreal>(5.0, qBound(0.004, m_ratio, 1.0) * width());
        p.setBrush(QColor(m_hottest ? "#5b85f0" : "#3f6ae0"));
        p.drawRoundedRect(QRectF(0, 1, w, 6), 3, 3);
    }

private:
    qreal m_ratio = 0.0;
    bool m_hottest = false;
};

// 一行卡片：[勾选?] 名称 ——占比条—— 大小，第二行完整路径；给了 onClick 即可点击下钻
class RowCard : public QWidget {
public:
    RowCard(const QString& name, const QString& subPath, bool checkable,
            std::function<void()> onClick, QWidget* parent = nullptr)
        : QWidget(parent), m_onClick(std::move(onClick))
    {
        setObjectName("row");
        setAttribute(Qt::WA_StyledBackground);
        if (m_onClick)
            setCursor(Qt::PointingHandCursor);

        auto* col = new QVBoxLayout(this);
        col->setContentsMargins(12, 8, 12, 8);
        col->setSpacing(3);

        auto* line = new QHBoxLayout;
        line->setSpacing(12);
        if (checkable) {
            check = new QCheckBox(this);
            check->setChecked(true);
            line->addWidget(check);
        }
        auto* nameLabel = new QLabel(name, this);
        nameLabel->setMinimumWidth(120);
        nameLabel->setMaximumWidth(230);
        line->addWidget(nameLabel);
        bar = new TrackBar(this);
        line->addWidget(bar, 1);
        sizeLabel = new QLabel(this);
        sizeLabel->setMinimumWidth(96);
        sizeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        line->addWidget(sizeLabel);
        col->addLayout(line);

        if (!subPath.isEmpty()) {
            auto* path = new QLabel(subPath, this);
            path->setObjectName("muted");
            QFont pf = path->font();
            pf.setPointSizeF(8.5);
            path->setFont(pf);
            col->addSpacing(2);
            col->addWidget(path);
        }
    }

    void setValues(qint64 bytes, qreal ratio, bool hottest)
    {
        sizeLabel->setText(human(bytes));
        bar->setRatio(ratio, hottest);
    }

    QCheckBox* check = nullptr;
    TrackBar* bar = nullptr;
    QLabel* sizeLabel = nullptr;

protected:
    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && m_onClick) {
            // 下钻会销毁包括本行在内的所有行：绝不能在自身的 mousePressEvent 里同步执行，
            // 否则返回基类处理时 this 已析构。排到本次事件之后再跑。
            auto cb = m_onClick;
            QMetaObject::invokeMethod(this, [cb] { cb(); }, Qt::QueuedConnection);
            e->accept();
            return;
        }
        QWidget::mousePressEvent(e);
    }

private:
    std::function<void()> m_onClick;
};

QLabel* sectionTitle(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    QFont f = l->font();
    f.setPointSizeF(10.5);
    f.setBold(true);
    l->setFont(f);
    return l;
}

// ---- WizTree 式矩形树图（squarified treemap）----

struct TCell {
    QRectF rect;
    int node = -2;        // -1 = 本目录直属文件，>=0 = 子目录节点
    qint64 bytes = 0;
    QString label;
    QColor color;
};

qreal worstRatio(const QVector<qreal>& row, qreal sum, qreal side)
{
    if (row.isEmpty() || sum <= 0.0 || side <= 0.0)
        return 1e18;
    qreal mx = 0.0, mn = 1e30;
    for (qreal v : row) {
        mx = qMax(mx, v);
        mn = qMin(mn, v);
    }
    const qreal s2 = sum * sum, w2 = side * side;
    return qMax(w2 * mx / s2, s2 / (w2 * mn));
}

class Treemap : public QWidget {
public:
    std::function<void(int)> onPick;

    void setData(const DiskScan::Tree* tree, int node)
    {
        m_tree = tree;
        m_node = node;
        m_hover = -2;
        relayout();
        update();
    }
    void clearData()
    {
        m_tree = nullptr;
        m_node = -1;
        m_cells.clear();
        update();
    }
    QSize minimumSizeHint() const override { return {320, 220}; }

protected:
    void resizeEvent(QResizeEvent*) override { relayout(); }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x17, 0x18, 0x1c));
        if (m_cells.isEmpty()) {
            p.setPen(QColor(0x8b, 0x93, 0xa1));
            p.drawText(rect(), Qt::AlignCenter,
                       QStringLiteral("点「整理」后这里显示占用矩形图"));
            return;
        }
        for (const TCell& c : m_cells) {
            p.fillRect(c.rect, c.color);
            p.setPen(QColor(0x17, 0x18, 0x1c));
            p.drawRect(c.rect.toAlignedRect());
        }
        for (const TCell& c : m_cells) {   // 文字后画，保证压在色块上
            if (c.rect.width() < 54 || c.rect.height() < 16)
                continue;
            QFont f = p.font();
            f.setPointSizeF(8.5);
            f.setBold(true);
            p.setFont(f);
            p.setPen(Qt::white);
            const QRect r = c.rect.toRect().adjusted(5, 3, -4, -2);
            p.drawText(r, Qt::AlignLeft | Qt::AlignTop | Qt::TextEllipsis | Qt::TextDontClip,
                       c.label);
            if (c.rect.height() >= 32) {
                QFont sf = p.font();
                sf.setBold(false);
                sf.setPointSizeF(8.0);
                p.setFont(sf);
                p.setPen(QColor(0xf0, 0xf0, 0xf0, 225));
                p.drawText(r.adjusted(0, 14, 0, 0),
                           Qt::AlignLeft | Qt::AlignTop | Qt::TextEllipsis | Qt::TextDontClip,
                           human(c.bytes));
            }
        }
        if (m_hover >= 0 && m_hover < m_cells.size()) {
            p.setPen(QPen(Qt::white, 2));
            p.setBrush(Qt::NoBrush);
            p.drawRect(m_cells[m_hover].rect.toAlignedRect().adjusted(1, 1, -1, -1));
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override
    {
        int hit = -1;
        for (int i = 0; i < m_cells.size(); ++i) {
            if (m_cells[i].rect.contains(QPointF(e->pos()))) {
                hit = i;
                break;
            }
        }
        if (hit != m_hover) {
            m_hover = hit;
            update();
        }
        if (hit >= 0 && m_tree && m_node >= 0) {
            const TCell& c = m_cells[hit];
            const qint64 total = m_tree->size[m_node] > 0 ? m_tree->size[m_node] : 1;
            const QString what = c.node < 0 ? QStringLiteral("本目录文件") : m_tree->name[c.node];
            QToolTip::showText(e->globalPosition().toPoint(),
                               QString("%1\n%2（%3%）\n%4")
                                   .arg(what, human(c.bytes),
                                        QString::number(c.bytes * 100.0 / total, 'f', 1),
                                        c.node >= 0 ? QStringLiteral("点击进入该目录")
                                                    : QString()));
        } else {
            QToolTip::hideText();
        }
    }

    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (e->button() != Qt::LeftButton || !m_tree)
            return;
        for (const TCell& c : m_cells) {
            if (c.node >= 0 && c.rect.contains(QPointF(e->pos())) && onPick) {
                auto cb = onPick;
                const int node = c.node;
                QMetaObject::invokeMethod(this, [cb, node] { cb(node); }, Qt::QueuedConnection);
                return;
            }
        }
    }

    void leaveEvent(QEvent*) override
    {
        m_hover = -2;
        QToolTip::hideText();
        update();
    }

private:
    void relayout()
    {
        m_cells.clear();
        if (!m_tree || m_node < 0 || width() < 8 || height() < 8)
            return;
        const DiskScan::Tree& t = *m_tree;
        const qint64 total = t.size[m_node];
        if (total <= 0)
            return;

        QVector<int> nodes;
        QVector<qreal> areas;
        const qreal full = qreal(width()) * qreal(height());
        const auto push = [&](int node, qint64 bytes) {
            if (bytes <= 0)
                return;
            nodes.append(node);
            areas.append(qreal(bytes) / qreal(total) * full);
        };
        push(-1, t.fileBytes[m_node]);
        for (int i = t.firstKid[m_node]; i < t.firstKid[m_node] + t.kidCount[m_node]; ++i)
            push(t.kids[i], t.size[t.kids[i]]);
        if (nodes.isEmpty())
            return;

        QRectF rest(rect());
        int i = 0;
        while (i < areas.size() && rest.width() > 2.0 && rest.height() > 2.0) {
            const bool wide = rest.width() >= rest.height();   // 条带沿短边铺开
            const qreal side = wide ? rest.height() : rest.width();
            QVector<qreal> row;
            qreal sum = 0.0, worst = 1e18;
            int j = i;
            while (j < areas.size()) {
                QVector<qreal> trial = row;
                trial.append(areas[j]);
                const qreal w = worstRatio(trial, sum + areas[j], side);
                if (!row.isEmpty() && w > worst)
                    break;
                row = trial;
                sum += areas[j];
                worst = w;
                ++j;
            }
            const qreal thick = sum / side;
            qreal off = 0.0;
            for (int k = 0; k < row.size(); ++k) {
                const qreal span = row[k] / thick;
                TCell c;
                c.rect = wide ? QRectF(rest.x(), rest.y() + off, thick, span)
                              : QRectF(rest.x() + off, rest.y(), span, thick);
                off += span;
                const int node = nodes[i + k];
                c.node = node;
                c.bytes = node < 0 ? t.fileBytes[m_node] : t.size[node];
                c.label = node < 0 ? QStringLiteral("本目录文件") : t.name[node];
                const uint hue = node < 0 ? 210u : uint((node * 47u) % 360u);
                c.color = QColor::fromHsv(int(hue), 145,
                                          node < 0 ? 95 : 140 + int(node % 5) * 14);
                m_cells.append(c);
            }
            rest = wide ? QRectF(rest.x(), rest.y() + thick, rest.width(), rest.height() - thick)
                        : QRectF(rest.x() + thick, rest.y(), rest.width() - thick, rest.height());
            i = j;
        }
    }

    const DiskScan::Tree* m_tree = nullptr;
    int m_node = -1;
    int m_hover = -2;
    QVector<TCell> m_cells;
};

} // namespace

DiskPage::DiskPage(QWidget* parent)
    : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(14, 10, 14, 10);
    outer->setSpacing(10);

    // ---- 汇总卡 ----
    auto* summary = new QWidget(this);
    summary->setObjectName("row");
    summary->setAttribute(Qt::WA_StyledBackground);
    auto* grid = new QGridLayout(summary);
    grid->setContentsMargins(16, 12, 16, 12);
    grid->setHorizontalSpacing(24);
    grid->setVerticalSpacing(2);
    m_cleanTotal = new QLabel("点「整理」开始", summary);
    m_diskUsed = new QLabel(summary);
    m_diskFree = new QLabel(summary);
    for (QLabel* v : {m_cleanTotal, m_diskUsed, m_diskFree}) {
        QFont f = v->font();
        f.setPointSizeF(12.5);
        f.setBold(true);
        v->setFont(f);
        v->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }
    const char* titles[] = {"可清理合计", "C 盘已用", "剩余空间"};
    for (int i = 0; i < 3; ++i) {
        auto* t = new QLabel(titles[i], summary);
        t->setObjectName("muted");
        QFont tf = t->font();
        tf.setPointSizeF(8.5);
        t->setFont(tf);
        grid->addWidget(t, 0, i);
    }
    grid->addWidget(m_cleanTotal, 1, 0);
    grid->addWidget(m_diskUsed, 1, 1);
    grid->addWidget(m_diskFree, 1, 2);
    grid->setColumnStretch(0, 3);
    grid->setColumnStretch(1, 2);
    grid->setColumnStretch(2, 2);

    // ---- 操作条 ----
    auto* actions = new QHBoxLayout;
    actions->setSpacing(8);
    m_analyzeBtn = new QPushButton("整理", this);
    m_analyzeBtn->setObjectName("primary");
    m_analyzeBtn->setToolTip("扫描常见可清理位置的占用，并分析 C 盘各目录占用");
    m_cleanBtn = new QPushButton("清理选中项", this);
    m_cleanBtn->setEnabled(false);   // 扫出结果前不给清理，避免空列表也去清空回收站
    m_status = new QLabel(this);
    m_status->setObjectName("muted");
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    actions->addWidget(m_analyzeBtn);
    actions->addWidget(m_cleanBtn);
    actions->addWidget(m_status, 1);

    // ---- 两个 tab ----
    m_tabs = new QTabBar(this);
    m_tabs->addTab("可清理位置");
    m_tabs->addTab("占用分析");
    m_tabs->setExpanding(false);
    m_stack = new QStackedWidget(this);

    auto* cleanPage = new QWidget(m_stack);
    auto* cleanLay = new QVBoxLayout(cleanPage);
    cleanLay->setContentsMargins(0, 6, 0, 0);
    cleanLay->setSpacing(6);
    m_rowsLay = new QVBoxLayout;
    m_rowsLay->setSpacing(6);
    m_rowsLay->addStretch(1);
    cleanLay->addLayout(m_rowsLay, 1);

    auto* anaPage = new QWidget(m_stack);
    auto* anaLay = new QVBoxLayout(anaPage);
    anaLay->setContentsMargins(0, 6, 0, 0);
    anaLay->setSpacing(8);
    auto* dirHead = new QHBoxLayout;
    dirHead->setSpacing(10);
    m_upBtn = new QPushButton("‹ 上一层", anaPage);
    m_upBtn->setEnabled(false);
    m_upBtn->setToolTip("返回上一级目录（已在根目录时不可用）");
    m_analysisPath = new QLabel("未开始分析", anaPage);
    m_analysisPath->setObjectName("muted");
    m_analysisPath->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_analysisStatus = new QLabel(anaPage);
    m_analysisStatus->setObjectName("muted");
    m_analysisStatus->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    dirHead->addWidget(m_upBtn);
    dirHead->addWidget(m_analysisPath, 1);
    dirHead->addWidget(m_analysisStatus);
    anaLay->addLayout(dirHead);

    m_map = new Treemap(anaPage);
    m_map->onPick = [this](int node) { showNode(node); };
    anaLay->addWidget(m_map, 3);

    auto* listHead = new QHBoxLayout;
    listHead->addWidget(sectionTitle("本层目录", anaPage));
    listHead->addStretch(1);
    anaLay->addLayout(listHead);
    auto* listScroll = new QScrollArea(anaPage);
    listScroll->setWidgetResizable(true);
    listScroll->setFrameShape(QFrame::NoFrame);
    auto* listHost = new QWidget(listScroll);
    listHost->setObjectName("rowsHost");
    m_analysisLay = new QVBoxLayout(listHost);
    m_analysisLay->setContentsMargins(0, 0, 0, 0);
    m_analysisLay->setSpacing(6);
    m_analysisLay->addStretch(1);
    listScroll->setWidget(listHost);
    anaLay->addWidget(listScroll, 2);

    m_stack->addWidget(cleanPage);
    m_stack->addWidget(anaPage);
    connect(m_tabs, &QTabBar::currentChanged, m_stack, &QStackedWidget::setCurrentIndex);

    outer->addWidget(summary);
    outer->addLayout(actions);
    outer->addWidget(m_tabs);
    outer->addWidget(m_stack, 1);

    connect(m_analyzeBtn, &QPushButton::clicked, this, &DiskPage::startAll);
    connect(m_cleanBtn, &QPushButton::clicked, this, &DiskPage::cleanSelected);
    connect(m_upBtn, &QPushButton::clicked, this, &DiskPage::analysisUp);

    m_timer = new QTimer(this);
    m_timer->setInterval(250);
    connect(m_timer, &QTimer::timeout, this, &DiskPage::tick);

    updateSummary();
    m_status->setText("点「整理」扫描可清理位置并分析目录占用；矩形图里点一块即可进入该目录。");
}

void DiskPage::updateSummary()
{
    ULARGE_INTEGER freeAvail{}, total{}, freeTotal{};
    if (!GetDiskFreeSpaceExW(L"C:\\", &freeAvail, &total, &freeTotal)) {
        m_diskUsed->setText("读取失败");
        m_diskFree->setText("-");
        return;
    }
    const qint64 bytesTotal = qint64(total.QuadPart);
    const qint64 bytesFree = qint64(freeTotal.QuadPart);
    m_diskUsed->setText(QString("%1 / %2").arg(human(bytesTotal - bytesFree),
                                               human(bytesTotal)));
    m_diskFree->setText(human(bytesFree));
}

void DiskPage::clearRows()
{
    while (m_rowsLay->count() > 1) {
        QLayoutItem* item = m_rowsLay->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    m_rows.clear();
}

void DiskPage::startAll()
{
    rescanClean();

    if (m_timer->isActive())
        return;   // 目录树还在扫，别重复起线程
    m_task = std::make_shared<Task>();
    m_analyzeBtn->setEnabled(false);
    m_upBtn->setEnabled(false);
    m_analysisPath->setText("正在遍历 C 盘目录树…");
    m_analysisStatus->setText("已统计 0 B");
    m_map->clearData();
    for (QWidget* w : m_analysisRows)
        delete w;
    m_analysisRows.clear();

    auto task = m_task;
    std::thread([task] {
        auto tree = std::make_shared<DiskScan::Tree>(
            DiskScan::run("C:/", 1 << 20, [task](qint64 bytes) {
                task->bytes.store(bytes, std::memory_order_relaxed);
            }));
        task->tree = tree;
        task->done.store(true, std::memory_order_release);
    }).detach();
    m_timer->start();
}

// 轮询后台线程：进度用原子量、结果用 shared_ptr，回调不跨线程碰界面
void DiskPage::tick()
{
    if (!m_task) {
        m_timer->stop();
        return;
    }
    if (!m_task->done.load(std::memory_order_acquire)) {
        m_analysisStatus->setText("已统计 "
                                  + human(m_task->bytes.load(std::memory_order_relaxed))
                                  + "，扫描中…");
        return;
    }
    m_timer->stop();
    m_tree = m_task->tree;
    m_task.reset();
    m_analyzeBtn->setEnabled(true);
    if (!m_tree || m_tree->isEmpty()) {
        m_analysisPath->setText("遍历失败（权限或路径问题）");
        return;
    }
    m_node = m_tree->root;
    renderAnalysis();
}

void DiskPage::showNode(int node)
{
    if (!m_tree || node < 0 || node >= m_tree->name.size())
        return;
    m_node = node;
    renderAnalysis();
}

void DiskPage::analysisUp()
{
    if (!m_tree || m_node < 0 || m_node == m_tree->root)
        return;
    showNode(m_tree->parent[m_node]);
}

void DiskPage::renderAnalysis()
{
    if (!m_tree || m_node < 0)
        return;
    const DiskScan::Tree& t = *m_tree;
    m_upBtn->setEnabled(m_node != t.root);
    m_analysisPath->setText(t.pathOf(m_node));
    m_map->setData(&t, m_node);

    for (QWidget* w : m_analysisRows)
        delete w;
    m_analysisRows.clear();

    const int count = t.kidCount[m_node];
    qint64 maxSz = 1;
    for (int i = 0; i < count; ++i)
        maxSz = qMax(maxSz, t.size[t.kids[t.firstKid[m_node] + i]]);
    const int shown = qMin(count, 80);
    for (int i = 0; i < shown; ++i) {
        const int kid = t.kids[t.firstKid[m_node] + i];
        const qint64 bytes = t.size[kid];
        auto* row = new RowCard(t.name[kid], t.pathOf(kid), false,
                                [this, kid] { showNode(kid); }, this);
        row->setValues(bytes, bytes > 0 ? qreal(bytes) / maxSz : 0.0, bytes == maxSz);
        m_analysisLay->insertWidget(m_analysisLay->count() - 1, row);
        m_analysisRows.append(row);
    }
    QString status = QString("本层 %1 个子目录，合计 %2").arg(count).arg(human(t.size[m_node]));
    if (count > shown)
        status += QString("（列表只显示前 %1，矩形图里全都有）").arg(shown);
    if (m_node == t.root && t.pruned > 0)
        status += QString("，另有 %1 个小于 1 MB 的目录未列出").arg(t.pruned);
    m_analysisStatus->setText(status);
}

void DiskPage::rescanClean()
{
    clearRows();
    m_cleanBtn->setEnabled(false);
    m_status->setText("正在扫描可清理位置…");

    const QVector<CleanItem> items = cleanItems();
    auto future = QtConcurrent::run([items]() -> QVector<QPair<CleanItem, qint64>> {
        QVector<QPair<CleanItem, qint64>> out;
        for (const CleanItem& c : items)
            out.append({c, QDir(c.path).exists() ? dirSize(c.path) : -1});
        return out;
    });
    auto* watcher = new QFutureWatcher<QVector<QPair<CleanItem, qint64>>>(this);
    connect(watcher, &QFutureWatcher<QVector<QPair<CleanItem, qint64>>>::finished, this,
            [this, watcher] {
                const auto results = watcher->result();
                watcher->deleteLater();
                qint64 maxSz = 1;
                for (const auto& p : results)
                    maxSz = qMax(maxSz, p.second);
                for (const auto& p : results) {
                    CleanRow cr;
                    cr.path = QDir::toNativeSeparators(p.first.path);
                    cr.admin = p.first.admin;
                    const qreal ratio = p.second > 0 ? qreal(p.second) / maxSz : 0.0;
                    auto* row = new RowCard(QString::fromUtf8(p.first.name), cr.path, true,
                                            nullptr, this);
                    row->setValues(p.second, ratio, p.second > 0 && p.second == maxSz);
                    if (cr.admin)
                        row->sizeLabel->setToolTip("清理该项需要管理员权限");
                    cr.box = row->check;
                    cr.bar = row->bar;
                    cr.size = row->sizeLabel;
                    m_rowsLay->insertWidget(m_rowsLay->count() - 1, row);
                    m_rows.append(cr);
                }
                qint64 total = 0;
                for (const auto& p : results)
                    if (p.second > 0)
                        total += p.second;
                m_cleanTotal->setText(human(total));
                m_cleanBtn->setEnabled(true);
                m_status->setText(QString("可清理位置合计 %1（清理前请确认相关软件已关闭）")
                                      .arg(human(total)));
            });
    watcher->setFuture(future);
}

void DiskPage::cleanSelected()
{
    if (QMessageBox::question(this, "ltool", "确定清理勾选的项目吗？此操作不可恢复。")
        != QMessageBox::Yes)
        return;
    qint64 freed = 0;
    QString skipped;
    for (const CleanRow& cr : m_rows) {
        if (!cr.box->isChecked())
            continue;
        QString err;
        freed += clearDir(cr.path, &err);
        if (!err.isEmpty())
            skipped += err;
    }
    SHEmptyRecycleBinW(nullptr, L"C:\\",
                       SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
    QMessageBox::information(this, "ltool",
        QString("已释放 %1 磁盘空间。%2").arg(human(freed),
              skipped.isEmpty() ? QString() : "\n部分文件被跳过：\n" + skipped));
    updateSummary();
    rescanClean();
}
