import io

P = r'D:\project\cpp\ltool\src\DiskPage.cpp'
s = io.open(P, encoding='utf-8').read()

USAGE_ROW = '''// 占用条形行：自绘"名称 + 满宽轨道 + 大小/占比"，点击=进入该目录看下一层
class UsageRow : public QWidget {
public:
    explicit UsageRow(std::function<void()> onClick, QWidget* parent = nullptr)
        : QWidget(parent), m_onClick(std::move(onClick))
    {
        setMouseTracking(true);
        if (m_onClick)
            setCursor(Qt::PointingHandCursor);
    }

    void setValues(const QString& name, qint64 bytes, qreal ratio)
    {
        m_name = name;
        m_bytes = bytes;
        m_ratio = ratio;
        update();
    }

    QSize sizeHint() const override { return QSize(0, 40); }
    QSize minimumSizeHint() const override { return QSize(0, 40); }

protected:
    void paintEvent(QEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRect card = rect().adjusted(0, 0, -1, -4);
        p.setPen(QColor(m_hover ? "#3a3c45" : "#2c2d34"));
        p.setBrush(m_hover ? QColor("#24252b") : QColor("#1f2025"));
        p.drawRoundedRect(card, 8, 8);

        const int pad = 12;
        const int rightCol = 116;
        const int nameW = qMax(110, int(card.width() * 0.28));

        QFont nameFont = font();
        nameFont.setPointSizeF(10.0);
        p.setFont(nameFont);
        p.setPen(QColor("#dce0e9"));
        p.drawText(QRect(card.left() + pad, card.top(), nameW, card.height()),
                   Qt::AlignLeft | Qt::AlignVCenter | Qt::ElideRight, m_name);

        const int trackL = card.left() + pad + nameW + 14;
        const int trackW = qMax(6, card.right() - pad - rightCol - trackL);
        const QRect track(trackL, card.center().y() - 4, trackW, 8);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#2b2d35"));
        p.drawRoundedRect(track, 4, 4);
        if (m_bytes > 0) {
            const int filled = qMax(5, int(qRound(qBound(0.006, m_ratio, 1.0) * trackW)));
            p.setBrush(QColor(m_ratio > 0.999 ? "#5b85f0" : "#3f6ae0"));
            p.drawRoundedRect(QRect(track.left(), track.top(), filled, track.height()), 4, 4);
        }

        const QRect rightColRect(card.right() - pad - rightCol, card.top(),
                                 rightCol, card.height());
        QFont sizeFont = font();
        sizeFont.setPointSizeF(10.0);
        p.setFont(sizeFont);
        p.setPen(QColor("#c9cfdd"));
        p.drawText(QRect(rightColRect.left(), rightColRect.top() + 5,
                         rightColRect.width(), 16),
                   Qt::AlignRight | Qt::AlignVCenter, human(m_bytes));
        QFont pctFont = font();
        pctFont.setPointSizeF(8.5);
        p.setFont(pctFont);
        p.setPen(QColor("#7d8595"));
        p.drawText(QRect(rightColRect.left(), rightColRect.bottom() - 13,
                         rightColRect.width(), 14),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(m_ratio * 100.0, 'f', 1) + '%');
    }

    void enterEvent(QEnterEvent* e) override
    {
        m_hover = true;
        QWidget::enterEvent(e);
    }

    void leaveEvent(QEvent* e) override
    {
        m_hover = false;
        QWidget::leaveEvent(e);
    }

    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && m_onClick)
            m_onClick();
        QWidget::mousePressEvent(e);
    }

private:
    std::function<void()> m_onClick;
    QString m_name;
    qint64 m_bytes = 0;
    qreal m_ratio = 0.0;
    bool m_hover = false;
};
'''

RIGHT_PANE = '''    auto* right = new QWidget(split);
    auto* rightLay = new QVBoxLayout(right);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->setSpacing(6);

    auto* headRow = new QHBoxLayout;
    headRow->setSpacing(8);
    m_upBtn = new QPushButton("\\u2039 上一层", right);
    m_upBtn->setEnabled(false);
    m_upBtn->setToolTip("返回上一级目录（已在根目录时不可用）");
    m_analysisPath = new QLabel("未开始分析", right);
    m_analysisPath->setObjectName("muted");
    m_analysisPath->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    {
        QFont cf = m_analysisPath->font();
        cf.setPointSizeF(10.5);
        cf.setBold(true);
        m_analysisPath->setFont(cf);
    }
    m_analysisStatus = new QLabel(right);
    m_analysisStatus->setObjectName("muted");
    m_analysisStatus->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    headRow->addWidget(m_upBtn);
    headRow->addWidget(m_analysisPath, 1);
    headRow->addWidget(m_analysisStatus);
    rightLay->addLayout(headRow);

    auto* analysisHost = new QWidget(right);
    analysisHost->setObjectName("rowsHost");
    m_analysisLay = new QVBoxLayout(analysisHost);
    m_analysisLay->setContentsMargins(0, 0, 0, 0);
    m_analysisLay->setSpacing(3);
    m_analysisLay->addStretch(1);
    auto* ascroll = new QScrollArea(right);
    ascroll->setWidgetResizable(true);
    ascroll->setWidget(analysisHost);
    ascroll->setFrameShape(QFrame::NoFrame);
    ascroll->setStyleSheet("QScrollArea{background:transparent;border:none;}");
    rightLay->addWidget(ascroll, 1);
'''

RENDER = '''// 只销毁自己登记的条形行；头部路径/合计标签不在本布局内，不会被误删
void DiskPage::renderAnalysis(const QVector<QPair<QString, qint64>>& rows, const QString& root)
{
    for (QWidget* w : m_analysisRows)
        delete w;
    m_analysisRows.clear();

    qint64 maxSz = 1;
    for (const auto& p : rows)
        maxSz = qMax(maxSz, p.second);
    for (int i = 0; i < rows.size(); ++i) {
        const auto& p = rows[i];
        const qreal ratio = p.second > 0 ? qreal(p.second) / maxSz : 0.0;
        const QString child = root + p.first;
        auto* row = new UsageRow([this, child] { scanAnalysis(child); }, this);
        row->setValues(p.first, p.second, ratio);
        row->setToolTip(QString("%1\\n%2\\n点击进入该目录")
                            .arg(QDir::toNativeSeparators(child), human(p.second)));
        m_analysisLay->insertWidget(m_analysisLay->count() - 1, row);
        m_analysisRows.append(row);
    }
}
'''

subs = []

# 1) includes
subs.append(('#include <QMessageBox>\n#include <QMouseEvent>\n',
             '#include <QMessageBox>\n#include <QMouseEvent>\n#include <QPainter>\n'))

# 2) BarRow + makeBarRow -> UsageRow
old_block_start = s.index('// 可点击的条形行：点击=进入该目录继续分析下一层')
old_block_end = s.index('// 一层目录的分析：results 与计数器由所有 finished 回调共享')
s = s[:old_block_start] + USAGE_ROW + '\n' + s[old_block_end:]

# 3) drop the rainbow palette (unused now)
subs.append(('''const char* const kAnalysisColors[] = {"#3f6ae0", "#2ea058", "#d6800f", "#8857e5",
                                       "#1f8dc0", "#d64545", "#57a028", "#7a8494"};

''', ''))

# 4) right pane ctor block
rp_start = s.index('    auto* right = new QWidget(split);')
rp_end = s.index('    split->addWidget(left);')
s = s[:rp_start] + RIGHT_PANE + '\n' + s[rp_end:]

# 5) scanAnalysis header/status wording
subs.append(('''    m_analysisPath->setText(QString("当前目录：%1（点击条目进入该目录）")
                                .arg(QDir::toNativeSeparators(m_analysisRoot)));
    m_analysisStatus->setText("分析中…每个子目录完成后立即显示");
    renderAnalysis({}, m_analysisRoot);

    const auto subs = QDir(m_analysisRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot
                                                    | QDir::NoSymLinks);
    if (subs.isEmpty()) {
        m_analysisStatus->setText("该目录下没有子目录");''',
 '''    m_analysisPath->setText(QDir::toNativeSeparators(m_analysisRoot));
    m_analysisStatus->setText("分析中…");
    renderAnalysis({}, m_analysisRoot);

    const auto subs = QDir(m_analysisRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot
                                                    | QDir::NoSymLinks);
    if (subs.isEmpty()) {
        m_analysisPath->setText(QDir::toNativeSeparators(m_analysisRoot) + "（无子目录）");
        m_analysisStatus->setText("0 项");'''))

subs.append(('''                        m_analyzeBtn->setEnabled(true);
                        m_analysisStatus->setText(
                            QString("分析完成：%1 个子目录，合计 %2")
                                .arg(total)
                                .arg(human(sum)));
                    } else {
                        m_analysisStatus->setText(
                            QString("分析中…已完成 %1/%2").arg(total - state->remaining)
                                .arg(total));
                    }''',
 '''                        m_analyzeBtn->setEnabled(true);
                        m_analysisPath->setText(
                            QDir::toNativeSeparators(base) + "   "
                            + QString::number(total) + " 个子目录");
                        m_analysisStatus->setText("合计 " + human(sum));
                    } else {
                        m_analysisStatus->setText(
                            QString("分析中 %1/%2").arg(total - state->remaining)
                                .arg(total));
                    }'''))

# 6) renderAnalysis body
ra_start = s.index('// 只销毁自己登记的条形行')
s = s[:ra_start] + RENDER

# 7) idle hint wording
subs.append(('    m_analysisStatus->setText("点「整理」开始分析 C 盘目录占用。");',
             '    m_analysisStatus->setText("点「整理」开始");'))

for old, new in subs:
    assert old in s, 'MISS: ' + old[:60]
    s = s.replace(old, new, 1)

io.open(P, 'w', encoding='utf-8', newline='\n').write(s)
print('patched, length', len(s))
