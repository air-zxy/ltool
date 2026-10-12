#pragma once

#include <QColor>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>

class QGridLayout;
class QLabel;
class QStackedWidget;
class QToolButton;

// 一个工具条目。这张表是唯一来源：卡片墙按它画，搜索框也按它匹配，
// 加一个工具只需要在这里加一行。
struct ToolDef {
    QString name;
    QString desc;
    QStringList keywords;   // 搜索框里的命中词（小写）
    QColor color;
    int embedded;           // >=0 = 内嵌页索引；-1 = JSON 窗口；-2 = Base64 窗口
};

// 配置 - 工具页：七张小卡片。
//   五张是内嵌页面（DLL 修复 / 广告弹窗 / 磁盘清理 / 网络 / 注册表分析），点进去在原地展开，带返回条；
//   两张是独立窗口（JSON 格式化 / Base64 编解码），点开的是各自的工具窗。
// 内嵌页面懒创建：DiskPage 这类页面构造时就会读盘/建 UI，不该在打开设置页时全建好。
class ToolsPage : public QWidget {
    Q_OBJECT
public:
    explicit ToolsPage(QWidget* parent = nullptr);

    static const QVector<ToolDef>& tools();
    void showEmbedded(int which);        // 搜索框也要用：直接展开某张内嵌卡片

private:
    QWidget* buildGrid();
    void addCard(QGridLayout* grid, int slot, const ToolDef& td);

    QStackedWidget* m_stack = nullptr;
    QToolButton* m_back = nullptr;
    QLabel* m_heading = nullptr;
    QWidget* m_embedded[7] = {};
};
