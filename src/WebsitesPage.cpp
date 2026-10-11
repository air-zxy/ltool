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

// 关键字那一格：逗号（中英文都算）、分号、空格随便用哪个分开
QStringList splitKeys(const QString& text)
{
    static const QRegularExpression sep(QStringLiteral("[,，;；\\s]+"));
    QStringList out;
    const QStringList parts = text.split(sep, Qt::SkipEmptyParts);
    for (const QString& p : parts)
        out << p.toLower();
    return out;
}

// 新增和编辑共用同一个表单；进来时 *e 是旧值，出去时是新值
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
        QStringLiteral("关键字：搜索框里第一个词命中它就打开这个网站。\n"
                       "搜索地址可留空；填了就把查询词写进 %s，例如 "
                       "https://github.com/search?q=%s"),
        &dlg);
    hint->setStyleSheet(QStringLiteral("color:#8b93a7;"));
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

} // namespace

WebsitesPage::WebsitesPage(QWidget* parent) : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(10);

    auto* tip = new QLabel(QStringLiteral("网站关键字：在搜索框输入 ld 直接打开 LINUX DO，"
                                          "输入 gh qt6 会在 GitHub 搜 qt6。"),
                           this);
    tip->setWordWrap(true);
    root->addWidget(tip);

    m_list = new QListWidget(this);
    m_list->setAlternatingRowColors(true);
    root->addWidget(m_list, 1);

    auto* btns = new QHBoxLayout;
    auto* addBtn = new QPushButton(QStringLiteral("新增"), this);
    auto* editBtn = new QPushButton(QStringLiteral("编辑"), this);
    auto* delBtn = new QPushButton(QStringLiteral("删除"), this);
    btns->addWidget(addBtn);
    btns->addWidget(editBtn);
    btns->addWidget(delBtn);
    btns->addStretch(1);
    root->addLayout(btns);

    connect(addBtn, &QPushButton::clicked, this, &WebsitesPage::addEntry);
    connect(editBtn, &QPushButton::clicked, this, &WebsitesPage::editEntry);
    connect(delBtn, &QPushButton::clicked, this, &WebsitesPage::removeEntry);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &WebsitesPage::editEntry);

    rebuild();
}

void WebsitesPage::rebuild()
{
    m_list->clear();
    const auto webs = WebShortcuts::loadWebs();
    for (int i = 0; i < webs.size(); ++i) {
        const WebEntry& e = webs[i];
        QString line = QStringLiteral("%1  [%2]  %3").arg(e.name, e.keys.join(QStringLiteral(" / ")),
                                                          e.url);
        if (!e.searchUrl.isEmpty())
            line += QStringLiteral("\n搜索：%1").arg(e.searchUrl);
        auto* it = new QListWidgetItem(line, m_list);
        it->setData(Qt::UserRole, i);
        it->setSizeHint(QSize(0, e.searchUrl.isEmpty() ? 34 : 52));
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

void WebsitesPage::editEntry()
{
    auto* it = m_list->currentItem();
    if (!it)
        return;
    auto webs = WebShortcuts::loadWebs();
    const int i = it->data(Qt::UserRole).toInt();
    if (i < 0 || i >= webs.size())
        return;
    WebEntry e = webs[i];
    if (!entryDialog(this, &e))
        return;
    webs[i] = e;
    WebShortcuts::saveWebs(webs);
    rebuild();
}

void WebsitesPage::removeEntry()
{
    auto* it = m_list->currentItem();
    if (!it)
        return;
    auto webs = WebShortcuts::loadWebs();
    const int i = it->data(Qt::UserRole).toInt();
    if (i < 0 || i >= webs.size())
        return;
    if (QMessageBox::question(this, QStringLiteral("删除网站"),
                              QStringLiteral("确定删掉 %1？").arg(webs[i].name))
        != QMessageBox::Yes)
        return;
    webs.removeAt(i);
    WebShortcuts::saveWebs(webs);
    rebuild();
}
