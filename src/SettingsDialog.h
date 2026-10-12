#pragma once
#include "core-api.h"

#include <QDialog>

class QListWidget;
class QStackedWidget;
class QToolButton;
class QTimer;
class ToolsPage;

// ltool 配置页面（搜索框右侧齿轮打开）：左侧菜单 + 右侧内容
//   语言 / 软件 / 网站 / 下载器 / 定时任务 / 工具 / 配置
class LTOOL_CORE_API SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    // 搜索框/托盘直接打开某个工具：跳到「工具」页并展开那张卡（工具现在全是 DLL 插件）。
    // text 非空 = 顺手把这段文本递给那个工具。返回 false = 那个 id 没加载起来。
    bool showTool(const QString& id, const QString& text = QString());
    // 直达某一页（可选里面的第几个 tab；toolId 非空就再展开那个工具的页）
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
    QWidget* m_pages[7] = {};
    bool m_built[7] = {};
    ToolsPage* m_toolsPage = nullptr;   // 行 5：搜索框要让它展开某个工具
    QToolButton* m_busy = nullptr;
    QTimer* m_busyTimer = nullptr;
    int m_busyFrame = 0;

protected:
    void showEvent(QShowEvent* e) override;
};
