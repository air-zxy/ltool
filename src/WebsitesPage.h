#pragma once

#include <QWidget>

class QListWidget;
class QPushButton;

// 配置 - 网站页：网站关键字列表（增加/编辑/删除，存 webs.json）
class WebsitesPage : public QWidget {
    Q_OBJECT
public:
    explicit WebsitesPage(QWidget* parent = nullptr);

private:
    void rebuild();
    void addEntry();
    void editEntry();
    void removeEntry();

    QListWidget* m_list = nullptr;
};
