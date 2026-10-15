#include "WebsitesPage.h"

#include "Utils.h"
#include "WebShortcuts.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace {

QColor colorForUrl(const QString& name, const QString& url)
{
    const size_t seed = qHash(name + url);
    return QColor::fromHsv(int(seed % 360), 120, 180);
}

QStringList splitKeys(const QString& text)
{
    static const QRegularExpression sep(QStringLiteral("[,，;；\\s]+"));
    QStringList out;
    const QStringList parts = text.split(sep, Qt::SkipEmptyParts);
    for (const QString& p : parts)
        out << p.toLower();
    return out;
}

bool entryDialog(QWidget* parent, WebEntry* e)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(e->name.isEmpty() ? QStringLiteral("新增网站")
                                         : QStringLiteral("编辑网站"));
    auto* lay = new QVBoxLayout(&dlg);
    auto* form = new QFormLayout;
    auto* name = new QLineEdit(e->name, &dlg);
    auto* keys = new QLineEdit(e->keys.join(QStringLiteral(", ")), &dlg);
    auto* url = new QLineEdit(e->url, &dlg);
    auto* search = new QLineEdit(e->searchUrl, &dlg);
    form->addRow(QStringLiteral("名称"), name);
    form->addRow(QStringLiteral("关键字"), keys);
    form->addRow(QStringLiteral("打开地址"), url);
    form->addRow(QStringLiteral("搜索地址"), search);
    lay->addLayout(form);

    auto* hint = new QLabel(
        QStringLiteral("关键字：搜索框里第一个词命中它就打开这个网站。多个关键字用逗号或空格隔开。\n"
                       "搜索地址可留空；填了就把查询词写进 %s，例如 https://github.com/search?q=%s"),
        &dlg);
    hint->setStyleSheet(QStringLiteral("color:#8b93a7; font-size: 9pt;"));
    lay->addWidget(hint);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    lay->addWidget(btns);
    QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted)
        return false;

    e->keys = splitKeys(keys->text());
    e->name = name->text().trimmed();
    e->url = WebShortcuts::normalizeUrl(url->text());
    e->searchUrl = search->text().trimmed();
    return !e->name.isEmpty() && !e->url.isEmpty() && !e->keys.isEmpty();
}

// 结构化网站行组件
class WebRowWidget : public QWidget {
public:
    WebRowWidget(const WebEntry& e, int index, WebsitesPage* page, QWidget* parent = nullptr)
        : QWidget(parent)
    {
        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(12, 8, 12, 8);
        lay->setSpacing(12);

        // 1. 图标 + 名字
        auto* icon = new QLabel(this);
        icon->setFixedSize(30, 30);
        icon->setPixmap(Utils::letterIcon(e.name, colorForUrl(e.name, e.url)).pixmap(30, 30));
        icon->setScaledContents(true);
        lay->addWidget(icon, 0, Qt::AlignVCenter);

        auto* nameLabel = new QLabel(e.name, this);
        nameLabel->setFixedWidth(130);
        QFont f = nameLabel->font();
        f.setBold(true);
        f.setPointSize(f.pointSize() + 1);
        nameLabel->setFont(f);
        lay->addWidget(nameLabel, 0, Qt::AlignVCenter);

        // 2. 触发关键字 Tags
        auto* keysWidget = new QWidget(this);
        auto* keysLay = new QHBoxLayout(keysWidget);
        keysLay->setContentsMargins(0, 0, 0, 0);
        keysLay->setSpacing(5);
        for (const QString& k : e.keys) {
            auto* tag = new QLabel(k, keysWidget);
            tag->setStyleSheet(QStringLiteral(
                "background: rgba(6, 182, 212, 0.16); "
                "color: #06b6d4; "
                "border: 1px solid rgba(6, 182, 212, 0.32); "
                "border-radius: 4px; "
                "padding: 1px 6px; "
                "font-size: 8.5pt; "
                "font-weight: 600;"));
            keysLay->addWidget(tag);
        }
        keysLay->addStretch(1);
        keysWidget->setFixedWidth(150);
        lay->addWidget(keysWidget, 0, Qt::AlignVCenter);

        // 3. 目标网址与搜索说明
        auto* urlCol = new QVBoxLayout;
        urlCol->setSpacing(2);
        urlCol->setContentsMargins(0, 0, 0, 0);

        auto* urlLabel = new QLabel(e.url, this);
        urlLabel->setStyleSheet(QStringLiteral("color: #d7dbe4; font-size: 9.5pt;"));
        urlLabel->setToolTip(e.url);
        urlCol->addWidget(urlLabel);

        if (!e.searchUrl.isEmpty()) {
            auto* searchLabel = new QLabel(QStringLiteral("🔍 搜索: %1").arg(e.searchUrl), this);
            searchLabel->setStyleSheet(QStringLiteral("color: #7d8698; font-size: 8.5pt;"));
            searchLabel->setToolTip(e.searchUrl);
            urlCol->addWidget(searchLabel);
        }
        lay->addLayout(urlCol, 1);

        // 4. 操作按钮（编辑 / 删除）
        auto* editBtn = new QPushButton(QStringLiteral("编辑"), this);
        editBtn->setObjectName(QStringLiteral("rowBtn"));
        editBtn->setCursor(Qt::PointingHandCursor);
        connect(editBtn, &QPushButton::clicked, page, [page, index] {
            page->editEntryAt(index);
        });
        lay->addWidget(editBtn, 0, Qt::AlignVCenter);

        auto* delBtn = new QPushButton(QStringLiteral("删除"), this);
        delBtn->setObjectName(QStringLiteral("rowBtn"));
        delBtn->setCursor(Qt::PointingHandCursor);
        connect(delBtn, &QPushButton::clicked, page, [page, index] {
            page->removeEntryAt(index);
        });
        lay->addWidget(delBtn, 0, Qt::AlignVCenter);
    }
};

} // namespace

WebsitesPage::WebsitesPage(QWidget* parent) : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(10);

    // 顶栏：标题说明与右上角新增按钮
    auto* topRow = new QHBoxLayout;
    topRow->setSpacing(10);

    auto* titleCol = new QVBoxLayout;
    titleCol->setSpacing(3);
    auto* heading = new QLabel(QStringLiteral("网站关键字直达"), this);
    heading->setObjectName(QStringLiteral("pageTitle"));
    auto* tip = new QLabel(QStringLiteral("输入关键字直接打开网址（如 ld 打开 LINUX DO），或输入关键词后按空格进行站内搜索（如 gh qt6）。"),
                           this);
    tip->setObjectName(QStringLiteral("tip"));
    tip->setWordWrap(true);
    titleCol->addWidget(heading);
    titleCol->addWidget(tip);
    topRow->addLayout(titleCol, 1);

    auto* addBtn = new QPushButton(QStringLiteral("+ 新增网站"), this);
    addBtn->setObjectName(QStringLiteral("primary"));
    addBtn->setCursor(Qt::PointingHandCursor);
    connect(addBtn, &QPushButton::clicked, this, &WebsitesPage::addEntry);
    topRow->addWidget(addBtn, 0, Qt::AlignVCenter);
    root->addLayout(topRow);

    // 表头栏
    auto* headRow = new QHBoxLayout;
    headRow->setContentsMargins(16, 4, 16, 2);
    headRow->setSpacing(12);

    auto* h1 = new QLabel(QStringLiteral("网站名称"), this);
    h1->setObjectName(QStringLiteral("colHead"));
    h1->setFixedWidth(172); // 30+130+12
    auto* h2 = new QLabel(QStringLiteral("触发关键字"), this);
    h2->setObjectName(QStringLiteral("colHead"));
    h2->setFixedWidth(150);
    auto* h3 = new QLabel(QStringLiteral("目标网址 / 搜索地址"), this);
    h3->setObjectName(QStringLiteral("colHead"));
    auto* h4 = new QLabel(QStringLiteral("操作"), this);
    h4->setObjectName(QStringLiteral("colHead"));
    h4->setFixedWidth(90);
    h4->setAlignment(Qt::AlignCenter);

    headRow->addWidget(h1);
    headRow->addWidget(h2);
    headRow->addWidget(h3, 1);
    headRow->addWidget(h4);
    root->addLayout(headRow);

    // 列表主体
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("webList"));
    m_list->setSpacing(3);
    root->addWidget(m_list, 1);

    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        if (it)
            editEntryAt(it->data(Qt::UserRole).toInt());
    });

    rebuild();
}

void WebsitesPage::rebuild()
{
    m_list->clear();
    const auto webs = WebShortcuts::loadWebs();
    for (int i = 0; i < webs.size(); ++i) {
        const WebEntry& e = webs[i];
        auto* it = new QListWidgetItem(m_list);
        it->setData(Qt::UserRole, i);
        it->setSizeHint(QSize(0, e.searchUrl.isEmpty() ? 50 : 62));
        auto* w = new WebRowWidget(e, i, this, m_list);
        m_list->setItemWidget(it, w);
    }
}

void WebsitesPage::addEntry()
{
    WebEntry e;
    if (!entryDialog(this, &e))
        return;
    auto webs = WebShortcuts::loadWebs();
    webs.append(e);
    WebShortcuts::saveWebs(webs);
    Utils::log(QString("webs: 新增 %1 (%2)").arg(e.name, e.keys.join(",")));
    rebuild();
}

void WebsitesPage::editEntryAt(int index)
{
    auto webs = WebShortcuts::loadWebs();
    if (index < 0 || index >= webs.size())
        return;
    WebEntry e = webs[index];
    if (!entryDialog(this, &e))
        return;
    webs[index] = e;
    WebShortcuts::saveWebs(webs);
    rebuild();
}

void WebsitesPage::removeEntryAt(int index)
{
    auto webs = WebShortcuts::loadWebs();
    if (index < 0 || index >= webs.size())
        return;
    if (QMessageBox::question(this, QStringLiteral("删除网站"),
                              QStringLiteral("确定删掉 %1？").arg(webs[index].name))
        != QMessageBox::Yes)
        return;
    webs.removeAt(index);
    WebShortcuts::saveWebs(webs);
    rebuild();
}
