#include "InfoRow.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QVBoxLayout>

QColor InfoRow::badgeColor(Badge b)
{
    switch (b) {
    case Good:    return QColor(96, 190, 120);
    case Warn:    return QColor(216, 168, 84);
    case Bad:     return QColor(224, 108, 108);
    case Muted:   return QColor(140, 148, 160);
    case Neutral: break;
    }
    return QColor(120, 150, 210);
}

InfoRow::InfoRow(const QString& title, const QString& subtitle, const QString& badge,
                 Badge kind, const QIcon& icon, int leftPad, QWidget* parent)
    : QWidget(parent), m_kind(kind)
{
    // 背景交给 QListWidget 画：这里透明，选中色才能透出来
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    // 点击要能落到列表上（选中一行），但**不能**给外层设 WA_TransparentForMouseEvents：
    // 父控件一旦穿透，Qt 的命中测试会跳过整棵子树，行里的勾选框就永远点不到。
    // 正确做法是只给那些纯展示的 QLabel 设穿透，外层保持可命中（它自己不吃事件，
    // 空白处照样冒泡回 viewport）。

    const QColor c = badgeColor(kind);

    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(leftPad, 5, 8, 5);
    lay->setSpacing(8);

    if (!badge.isEmpty()) {                                   // 没有结论就不画这个点，纯装饰
        auto* dot = new QLabel(this);
        dot->setFixedSize(8, 8);
        dot->setStyleSheet(QString("background:%1; border-radius:4px;").arg(c.name()));
        lay->addWidget(dot, 0, Qt::AlignVCenter);
    }

    if (!icon.isNull()) {
        m_icon = new QLabel(this);
        m_icon->setFixedSize(20, 20);
        m_icon->setPixmap(icon.pixmap(20, 20));
        m_icon->setScaledContents(true);
        m_icon->setAttribute(Qt::WA_TransparentForMouseEvents);
        lay->addWidget(m_icon, 0, Qt::AlignVCenter);
    }

    auto* text = new QVBoxLayout;
    text->setSpacing(1);
    auto* head = new QLabel(title, this);
    QFont f = head->font();
    f.setBold(true);
    head->setFont(f);
    head->setTextInteractionFlags(Qt::NoTextInteraction);
    // 横向 Ignored：让标题/副标题可以被压缩，不会因为一行长文本把整个列表撑出横向滚动条
    head->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* sub = new QLabel(subtitle, this);
    sub->setStyleSheet(QStringLiteral("color:#8b93a1;font-size:8.5pt;background:transparent;"));
    sub->setTextInteractionFlags(Qt::NoTextInteraction);
    sub->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    text->addWidget(head);
    text->addWidget(sub);
    lay->addLayout(text, 1);

    if (!badge.isEmpty()) {
        auto* b = new QLabel(badge, this);
        b->setStyleSheet(QString("color:%1; border:1px solid %2; border-radius:9px;"
                                 "padding:2px 9px;font-size:8.5pt;background:transparent;")
                             .arg(c.name(), c.name()));
        b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        lay->addWidget(b, 0, Qt::AlignVCenter);
    }

    for (QWidget* child : findChildren<QWidget*>())
        if (child != m_check)
            child->setAttribute(Qt::WA_TransparentForMouseEvents);
}

void InfoRow::setIconPixmap(const QPixmap& pix)
{
    if (pix.isNull())
        return;
    auto* lay = qobject_cast<QHBoxLayout*>(layout());
    if (!lay)
        return;
    if (!m_icon) {
        m_icon = new QLabel(this);
        m_icon->setFixedSize(20, 20);
        m_icon->setScaledContents(true);
        m_icon->setAttribute(Qt::WA_TransparentForMouseEvents);
        // 插到文字块之前：第 0 项可能是勾选框或色点，所以找第一个子布局的位置
        int at = lay->count();
        for (int i = 0; i < lay->count(); ++i) {
            QLayoutItem* item = lay->itemAt(i);
            if (item && item->layout()) { at = i; break; }
        }
        lay->insertWidget(at, m_icon, 0, Qt::AlignVCenter);
    }
    m_icon->setPixmap(pix);
}

void InfoRow::makeCheckable(bool checked)
{
    if (m_check)
        return;
    m_check = new QCheckBox(this);
    m_check->setChecked(checked);
    m_check->setCursor(Qt::PointingHandCursor);
    m_check->setStyleSheet(QStringLiteral("QCheckBox{spacing:0;background:transparent;}"));
    connect(m_check, &QCheckBox::toggled, this, &InfoRow::checkToggled);
    auto* lay = qobject_cast<QHBoxLayout*>(layout());
    if (lay)
        lay->insertWidget(0, m_check, 0, Qt::AlignVCenter);
}

bool InfoRow::isChecked() const
{
    return m_check && m_check->isChecked();
}
