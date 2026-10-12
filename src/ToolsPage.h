#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QStackedWidget;
class QToolButton;

// 配置 - 工具页。卡片墙**完全由插件目录决定**：`<exe 目录>\ltool-plugins\*.dll`。
// 九个自带工具（DLL 修复 / 广告弹窗 / 磁盘清理 / 网络 / 注册表分析 / 开机启动分析 /
// Windows 用户名 / JSON / Base64）现在也是插件，和别人写的插件走同一条路，没有特判。
//
// 画卡片只读元数据（plugin.json 嵌在 dll 里），点开才 LoadLibrary + createPage；
// 加载过的页缓存着复用，插件 dll 只装不卸。
// 目录空了 / 某个 dll 没通过检查 —— 界面上一行老实话说出来，不能默默一张卡都不显示。
class ToolsPage : public QWidget {
    Q_OBJECT
public:
    explicit ToolsPage(QWidget* parent = nullptr);

    // 搜索框 / 托盘都走这条：展开那张页，可选地把一段文本递给它。
    // 返回 false = 这个 id 没加载起来（错误已经进日志，界面上说了一句人话）。
    bool showTool(const QString& id, const QString& text = QString());

    void rebuildGrid();       // 重新扫插件目录之后重画卡片墙

private:
    QWidget* buildGrid();

    QStackedWidget* m_stack = nullptr;
    QToolButton* m_back = nullptr;
    QLabel* m_heading = nullptr;
    QWidget* m_grid = nullptr;    // stack 的第 0 格，就是卡片墙本身
};
