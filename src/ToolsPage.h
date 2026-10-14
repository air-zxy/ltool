#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

#include "PluginStore.h"     // 一张列表：每张卡片就是一个条目，带状态和那颗按钮

class QLabel;
class QPushButton;
class QStackedWidget;
class QToolButton;

// 插件商店。**一张列表**，卡片就是条目 —— 没有"已装的墙 + 能装的清单"两页。
//
// 这一面列什么只有一条规则：
//   · 还没拉清单 → 本机 `<exe 目录>\ltool-plugins\` 里装着的那几个（含改名成 .off 的）
//   · 拉过清单 → 以插件仓库那份 index.json 为准，后面再跟上"本机装着、清单里没列"的那几个
// 每张卡按状态给一颗按钮：没装的「装」、装了但哈希不同的「更新」再加「禁用」、装了的「禁用」、
// 被改名的「启用」；接口版本对不上的那条不给按钮，note 里说清是哪一侧旧。
//
// 点卡片正文只做这一张卡该做的那件事：本机真装着 dll 的就是打开那张页（点开才 LoadLibrary，
// 页缓存着复用，dll 只装不卸）；还没装的就是装它 —— 绝不能给一张点开没页可看的卡。
class ToolsPage : public QWidget {
    Q_OBJECT
public:
    explicit ToolsPage(QWidget* parent = nullptr);

    // 搜索框 / 托盘都走这条：展开那张页，可选地把一段文本递给它。
    // 返回 false = 这个 id 没加载起来（错误已经进日志，界面上说了一句人话）。
    bool showTool(const QString& id, const QString& text = QString());

    void rebuildGrid();      // 重扫目录之后重画那一面卡片（装完/禁用完走的就是这条）
    void refreshStore();     // 联网拉清单：判哈希/落盘/改名都在 PluginStore 那几个纯函数里

private:
    QVector<PluginStore::Row> shopRows() const;   // 这一面卡片的数据，唯一一条路
    QWidget* buildGrid();
    QWidget* makeCard(const PluginStore::Row& r);
    void installEntry(const PluginStore::Entry& e, QPushButton* btn, QLabel* state);

    QLabel* m_storeStatus = nullptr;
    bool m_storeBusy = false;
    // 存的是**清单本身**而不是画出来的那屏卡：rebuildGrid 整格换掉之后拿它重新比一次本地目录，
    // 刚装完/刚禁用完，状态就跟着目录里的文件动。
    QVector<PluginStore::Entry> m_lastEntries;
    QStringList m_lastProblems;
    bool m_haveStore = false;

    QStackedWidget* m_stack = nullptr;
    QToolButton* m_back = nullptr;
    QLabel* m_heading = nullptr;
};
