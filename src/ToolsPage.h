#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

#include "PluginStore.h"     // 商店那一栏要按条目画行，Entry/Row 在这里是完整类型

class QLabel;
class QPushButton;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;

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

    // 商店那一栏：刷新远端清单、把每个插件画成一行（状态 + 一个按钮）。
    // 联网只有"拉清单"和"下 dll"两段，判哈希/落盘/改名都交给 PluginStore 那几个纯函数，
    // 所以 --storetest 能一次不联网把整条判据跑完。
    void refreshStore();

private:
    QWidget* buildGrid();
    QWidget* buildStore();                                  // 卡片墙下面那一栏
    void showStoreRows(const QVector<PluginStore::Row>& rows, const QStringList& problems);
    void installEntry(const PluginStore::Entry& e, QPushButton* btn, QLabel* state);

    QLabel* m_storeStatus = nullptr;
    QVBoxLayout* m_storeRows = nullptr;
    bool m_storeBusy = false;
    // 卡片墙和商店在**同一格**里，而 rebuildGrid()（装完一个、禁用一个之后要重扫）是整格换掉的：
    // 缓存清单本身（不是画出来的那一屏行），回填时拿它**重新比一次本地目录** ——
    // 不然刚点完"禁用"，那一栏会退回"还没拉过清单"，说的和做的对不上。
    QVector<PluginStore::Entry> m_lastEntries;
    QStringList m_lastProblems;
    QString m_lastStatus;
    int m_lastExtra = 0;      // 清单外那几个的个数（状态行回填的是同一个数）
    bool m_haveStore = false;

    QStackedWidget* m_stack = nullptr;
    QToolButton* m_back = nullptr;
    QLabel* m_heading = nullptr;
    QWidget* m_grid = nullptr;    // stack 的第 0 格，就是卡片墙本身
};
