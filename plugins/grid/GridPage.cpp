#include "GridPage.h"

#include "GridBoard.h"
#include "Utils.h"

#include <QColorDialog>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QGuiApplication>
#include <QPixmap>
#include <QScreen>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {

constexpr int kOutGap = 24;         // 合成图上格子之间的缝（板上是 8，出图宽一点好看）

QString imageFilter()
{
    return QStringLiteral("图片 (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tif *.tiff)");
}

} // namespace

GridPage::GridPage(QWidget* parent)
    : QWidget(parent)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(12, 10, 12, 10);
    lay->setSpacing(8);

    auto* head = new QHBoxLayout;
    head->setSpacing(6);
    head->addWidget(new QLabel(QStringLiteral("格数："), this));
    const QVector<QPair<int, QString>> kinds = { { 4, QStringLiteral("四宫") },
                                                 { 6, QStringLiteral("六宫") },
                                                 { 9, QStringLiteral("九宫") } };
    for (const auto& k : kinds) {
        auto* b = new QPushButton(k.second, this);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::clicked, this, [this, n = k.first] { setSlots(n); });
        head->addWidget(b);
        m_slotBtns << b;
    }
    m_slotBtns.first()->setChecked(true);

    head->addSpacing(10);
    m_bgBtn = new QPushButton(QStringLiteral("背景色"), this);
    m_bgBtn->setCursor(Qt::PointingHandCursor);
    m_bgBtn->setMinimumWidth(96);
    connect(m_bgBtn, &QPushButton::clicked, this, &GridPage::pickColor);
    head->addWidget(m_bgBtn);

    auto* add = new QPushButton(QStringLiteral("添加图片…"), this);
    add->setCursor(Qt::PointingHandCursor);
    connect(add, &QPushButton::clicked, this, &GridPage::addFromFile);
    head->addWidget(add);

    auto* clear = new QPushButton(QStringLiteral("清空"), this);
    clear->setCursor(Qt::PointingHandCursor);
    connect(clear, &QPushButton::clicked, this, [this] {
        m_board->clearImages();
        showStatus(QStringLiteral("清空了。拖图片进格子，或者点「添加图片…」挑几个文件。"));
    });
    head->addWidget(clear);
    head->addStretch(1);

    auto* gen = new QPushButton(QStringLiteral("生成宫图"), this);
    gen->setCursor(Qt::PointingHandCursor);
    gen->setObjectName(QStringLiteral("gridGenerate"));
    connect(gen, &QPushButton::clicked, this, [this] { generate(); });
    head->addWidget(gen);
    lay->addLayout(head);

    m_board = new GridBoard(this);
    lay->addWidget(m_board, 1);
    connect(m_board, &GridBoard::status, this, &GridPage::showStatus);
    connect(m_board, &GridBoard::changed, this, [this] {
        // 顺序/内容一变，之前那张生成出来的图就不算数了：把"打开"两颗收回去
        if (!m_lastFile.isEmpty()) {
            m_lastFile.clear();
            m_openBtn->setEnabled(false);
            m_dirBtn->setEnabled(false);
        }
    });

    auto* foot = new QHBoxLayout;
    foot->setSpacing(6);
    m_status = new QLabel(QStringLiteral("拖图片进格子（支持一次拖多张）；从一格拖到另一格换顺序；点右上角 ✕ 拿掉那一张。"), this);
    m_status->setObjectName(QStringLiteral("muted"));
    m_status->setWordWrap(true);
    foot->addWidget(m_status, 1);
    m_openBtn = new QPushButton(QStringLiteral("打开这张"), this);
    m_openBtn->setCursor(Qt::PointingHandCursor);
    m_openBtn->setEnabled(false);
    m_openBtn->setObjectName(QStringLiteral("gridOpen"));
    connect(m_openBtn, &QPushButton::clicked, this, [this] {
        if (!m_lastFile.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_lastFile));
    });
    foot->addWidget(m_openBtn);
    m_dirBtn = new QPushButton(QStringLiteral("打开所在文件夹"), this);
    m_dirBtn->setCursor(Qt::PointingHandCursor);
    m_dirBtn->setEnabled(false);
    m_dirBtn->setObjectName(QStringLiteral("gridOpenDir"));
    connect(m_dirBtn, &QPushButton::clicked, this, [this] {
        if (!m_lastFile.isEmpty())
            Utils::revealInExplorer(m_lastFile);
    });
    foot->addWidget(m_dirBtn);
    lay->addLayout(foot);

    setBackgroundColor(m_bg);
}

void GridPage::setSlots(int n)
{
    m_board->setSlots(n);
    for (int i = 0; i < m_slotBtns.size(); ++i) {
        const int kinds[3] = { 4, 6, 9 };
        m_slotBtns[i]->setChecked(kinds[i] == n);
    }
}

int GridPage::slotCount() const
{
    return m_board->slotCount();
}

void GridPage::setBackgroundColor(const QColor& c)
{
    if (!c.isValid())
        return;
    m_bg = c;
    // 按钮上直接刷成那个颜色：让人看到"选的就是它"，而不是只有一句"背景色"
    const bool light = m_bg.lightnessF() > 0.6;
    m_bgBtn->setText(QStringLiteral("背景色 %1").arg(m_bg.name().toUpper()));
    m_bgBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:%1;color:%2;border:1px solid %3;border-radius:6px;padding:4px 10px;}"
        "QPushButton:hover{border:1px solid #3f6ae0;}")
        .arg(m_bg.name(), light ? QStringLiteral("#1b1d22") : QStringLiteral("#ffffff"),
             light ? QStringLiteral("#c9ced8") : QStringLiteral("#3a3d45")));
}

void GridPage::pickColor()
{
    const QColor c = QColorDialog::getColor(m_bg, this, QStringLiteral("选一个背景色"));
    if (!c.isValid())
        return;                                   // 取消就是取消，别把颜色改了还说改了
    setBackgroundColor(c);
    showStatus(QStringLiteral("背景色改成 %1 了。空格子就是这块颜色。").arg(c.name().toUpper()));
}

void GridPage::addFromFile()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("添加图片"), Utils::capturesDir(), imageFilter());
    if (files.isEmpty())
        return;
    m_board->addImages(files);
}

void GridPage::showStatus(const QString& text)
{
    m_status->setText(text);
}

QImage GridPage::compose(int cellPx, int gap, const QColor& bg) const
{
    const int cols = m_board->cols();
    const int rows = m_board->rows();
    const int w = cols * cellPx + (cols + 1) * gap;
    const int h = rows * cellPx + (rows + 1) * gap;
    QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
    out.fill(bg);
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QVector<QString> paths = m_board->paths();
    for (int i = 0; i < paths.size(); ++i) {
        if (paths[i].isEmpty())
            continue;                             // 空格子 = 只留背景色
        QImageReader r(paths[i]);
        r.setAutoTransform(true);                 // EXIF 方向：手机照片不转就是倒的
        // 原图 4000x3000 没必要整张吃进来 —— 出图只要 cellPx 见方那一大块。
        // 短边先缩到 cellPx，剩下的居中裁正方形，和板上预览同一条算法。
        const QSize hinted = r.size();
        if (hinted.isValid() && qMin(hinted.width(), hinted.height()) > cellPx) {
            const double k = double(cellPx) / qMin(hinted.width(), hinted.height());
            r.setScaledSize(QSize(qMax(1, qRound(hinted.width() * k)),
                                  qMax(1, qRound(hinted.height() * k))));
        }
        const QImage src = r.read();
        if (src.isNull()) {
            Utils::log(QStringLiteral("grid: %1 读不出来：%2").arg(paths[i], r.errorString()));
            continue;
        }
        const int side = qMin(src.width(), src.height());
        const QRect crop((src.width() - side) / 2, (src.height() - side) / 2, side, side);
        const int col = i % cols;
        const int row = i / cols;
        const QRect target(gap + col * (cellPx + gap), gap + row * (cellPx + gap), cellPx, cellPx);
        p.drawImage(target, src, crop);
    }
    p.end();
    return out;
}

QString GridPage::previewCaption(const QImage& img) const
{
    // 弹框上面那行字说的是"存出去的那张"，不是屏上这块缩图 —— 两边数字不一样是应该的，
    // 但必须写明白，不然人以为存下来也只有这么大。
    return QStringLiteral("存出去的是 %1×%2 原尺寸（屏上这块是缩到放得下的样子），%3 格，背景 %4，落在 %5")
        .arg(img.width()).arg(img.height()).arg(m_board->slotCount())
        .arg(m_bg.name().toUpper()).arg(Utils::capturesDir());
}

// 弹框本体单独做成"造出来"而不是"弹出来"：判据要能在不 exec 的前提下检查那两颗按钮、
// 那行字和那张缩图（离屏里 exec() 的返回值不作数，这条在卸载那一步已经栽过一次）。
QDialog* GridPage::previewDialog(const QImage& img) const
{
    auto* box = new QDialog(const_cast<GridPage*>(this));
    box->setObjectName(QStringLiteral("gridPreview"));
    box->setWindowTitle(QStringLiteral("效果图：看过再存"));
    auto* lay = new QVBoxLayout(box);
    lay->setContentsMargins(12, 10, 12, 10);
    lay->setSpacing(8);

    auto* cap = new QLabel(previewCaption(img), box);
    cap->setObjectName(QStringLiteral("gridPreviewCap"));
    cap->setWordWrap(true);

    const QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
    const int maxW = qMax(320, qMin(900, avail.width() - 260));
    const int maxH = qMax(240, qMin(620, avail.height() - 260));
    auto* shot = new QLabel(box);
    shot->setObjectName(QStringLiteral("gridPreviewShot"));
    shot->setAlignment(Qt::AlignCenter);
    shot->setMinimumSize(maxW, maxH);
    shot->setStyleSheet(QStringLiteral("background:#101114;border:1px solid #3a3c45;border-radius:6px;"));
    QPixmap pm = QPixmap::fromImage(img);
    // 只缩不放大：图本来就小（比如自测那张 150 见方）就按原尺寸摆，
    // 放大出来的糊图会让人以为存出去的也是糊的。
    if (pm.width() > maxW - 12 || pm.height() > maxH - 12)
        pm = pm.scaled(maxW - 12, maxH - 12, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    shot->setPixmap(pm);

    auto* btns = new QHBoxLayout;
    btns->setSpacing(6);
    btns->addStretch(1);
    auto* cancel = new QPushButton(QStringLiteral("取消（不存）"), box);
    cancel->setObjectName(QStringLiteral("gridCancel"));
    cancel->setCursor(Qt::PointingHandCursor);
    auto* save = new QPushButton(QStringLiteral("保存这张"), box);
    save->setObjectName(QStringLiteral("gridSave"));
    save->setCursor(Qt::PointingHandCursor);
    save->setDefault(true);
    connect(cancel, &QPushButton::clicked, box, &QDialog::reject);
    connect(save, &QPushButton::clicked, box, &QDialog::accept);
    btns->addWidget(cancel);
    btns->addWidget(save);

    lay->addWidget(cap);
    lay->addWidget(shot, 1);
    lay->addLayout(btns);
    box->resize(maxW + 40, maxH + 140);
    return box;
}

bool GridPage::confirmPreview(const QImage& img)
{
    QDialog* box = previewDialog(img);
    const bool ok = box->exec() == QDialog::Accepted;
    delete box;
    return ok;
}

QString GridPage::generate()
{
    if (m_board->filled() == 0) {
        showStatus(QStringLiteral("一张图都还没有 —— 先拖几张进来（或者点「添加图片…」）。"));
        return QString();
    }
    const QImage img = compose(m_cellPx, kOutGap, m_bg);
    const bool agreed = m_gate ? m_gate(img) : confirmPreview(img);
    if (!agreed) {
        // 取消就是取消：别写文件、别把"打开这张"点亮、别说成已生成
        showStatus(QStringLiteral("没存 —— 你在效果图那一步按了取消。摆的还在，改完再按一次「生成宫图」。"));
        return QString();
    }
    return saveComposed(img);
}

QString GridPage::saveComposed(const QImage& img)
{
    const QString dir = Utils::capturesDir();
    QDir().mkpath(dir);
    const QString name = QStringLiteral("grid-%1-%2宫.png")
                             .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"))
                             .arg(m_board->slotCount());
    const QString file = QDir(dir).absoluteFilePath(name);
    if (!img.save(file, "PNG")) {
        showStatus(QStringLiteral("写不下去：%1（看日志）").arg(file));
        Utils::log(QStringLiteral("grid: 写 %1 失败").arg(file));
        return QString();
    }
    m_lastFile = file;
    m_openBtn->setEnabled(true);
    m_dirBtn->setEnabled(true);
    const qint64 kb = QFileInfo(file).size() / 1024;
    showStatus(QStringLiteral("已生成 %1（%2×%3，%4 张图，背景 %5，%6 KB）")
                   .arg(QFileInfo(file).fileName()).arg(img.width()).arg(img.height())
                   .arg(m_board->filled()).arg(m_bg.name().toUpper()).arg(kb));
    Utils::log(QStringLiteral("grid: 生成 %1（%2x%3，%4/%5 格，背景 %6）")
                   .arg(file).arg(img.width()).arg(img.height()).arg(m_board->filled())
                   .arg(m_board->slotCount()).arg(m_bg.name()));
    emit generated(file);
    return file;
}
