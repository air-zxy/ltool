#pragma once
#include "core-api.h"

#include <QDialog>

class QListWidget;
class QStackedWidget;
class QToolButton;
class QTimer;
class ToolsPage;

// ltool 配置页面（搜索框右侧齿轮打开）：左侧菜单 + 右侧内容
//   网站 / 插件商店 / 配置 / 关于 —— 只有四行。
//   语言在「配置」那一页里的一节；软件 / 下载器 / 定时任务是插件商店里的卡片，
//   装了才出现、把 dll 改名 .off 就没了，和第三方插件同一条路。
class LTOOL_CORE_API SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    // 搜索框/托盘直接打开某个工具：跳到「插件商店」那一行并展开那张卡。
    // text 非空 = 顺手把这段文本递给那个工具。返回 false = 那个 id 没加载起来。
    bool showTool(const QString& id, const QString& text = QString());
    // 直达某一页（tab 是那一张页里第几个选项卡 —— 软件那两张页搬进插件之后，
    // 它藏在「插件商店」那张卡里面，所以先展开卡再挑 tab；toolId 非空就展开那张卡）
    void openAt(int menuRow, int tab = -1, const QString& toolId = QString());

signals:
    // 索引相关设置变化，需要触发一次重新扫描
    void rescanRequested();
    // 快捷键变更（序列字符串）
    void hotkeyChanged(const QString& sequence);

private:
    void ensurePage(int index);
    void buildBusyRow();
    void refreshBusy();     // 菜单左下角的加载标志：有后台静默安装在跑才出现

    QListWidget* m_menu = nullptr;
    QStackedWidget* m_stack = nullptr;
    bool m_built[4] = {};
    ToolsPage* m_toolsPage = nullptr;   // 行 1「插件商店」：搜索框要让它展开某个工具
    QToolButton* m_busy = nullptr;
    QTimer* m_busyTimer = nullptr;
    int m_busyFrame = 0;

protected:
    void showEvent(QShowEvent* e) override;
    // 配置页拨「夜间/日间」那颗时，Theme::setMode() 会给每个顶层窗口发一次 ThemeChange；
    // 这一页的样式表是挂在窗口上的（qApp 那份传导不下来），所以在这儿原地重算。
    bool event(QEvent* e) override;

private:
    void applyThemeQss();
    void reject() override;
};
