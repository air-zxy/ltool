#pragma once
#include "core-api.h"

#include <QPointer>
#include <QWidget>
#include <QElapsedTimer>
#include <QVector>
#include <QHash>
#include <QSet>

#include "AppScanner.h"
#include "DevEnv.h"
#include "RegClean.h"

class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QLabel;
class QToolButton;
class QTimer;
class QMenu;
class QFileSystemWatcher;

// 搜索结果条目
struct SearchResult {
    enum Kind {
        App,          // 应用：Enter 启动，target=目标 exe（可空），extra=lnk 路径
        File,         // 文件/文件夹：Enter 打开
        Web,          // 网址/网站关键字：Enter 用默认浏览器打开
        Command,      // 命令：rescan / init / path / reg
        PluginEntry,  // 开发环境插件入口
        PluginItem,   // 插件组件（安装/重检）
        RegIssue,     // 注册表问题（Enter 删除）
        Info,         // 纯信息行，不可激活
    };
    Kind kind = Info;
    QString title;
    QString subtitle;
    QString target;   // 语义见各 kind
    QString extra;    // 语义见各 kind
    QIcon icon;
    bool actionable = true;
    bool isSection = false;
};

class LTOOL_CORE_API SearchWindow : public QWidget {
    Q_OBJECT
public:
    explicit SearchWindow(QWidget* parent = nullptr);

    void toggleVisible();
    void showLauncher();
    void warmUp();   // 启动后先映射+绘制一遍，别把这份开销留给第一次按热键
    void rescan() { startScan(false, true); }        // 手动：强制全量重建
    void startInitialScan() { startScan(false, false); } // 启动：增量优先
    void setHotkeyLabel(const QString& label);
    void openSettings();
    // 弹设置页 → 工具页 → 展开那个插件的页（工具都是 DLL 插件，主程序不再 new 它们）。
    // text 非空 = 把这段递给那个工具（JSON 解析 / Base64 解码那两条）。
    void openTool(const QString& id, const QString& text = QString());
    // 托盘「关于」：弹设置页并翻到那一行（行号就是左侧菜单的顺序）
    void openSettingsPage(int menuRow);

    const SearchResult& resultAt(int i) const { return m_results[i]; }
    int resultCount() const { return m_results.size(); }   // 给离屏自测用（--pinyin 数第一行是谁）
    // 把还压在 120ms 去抖里的那一拍立刻补上（回车之前必须做，见 .cpp 里 activateCurrent 的注释）。
    // 公开是为了离屏判据能量到"回车用的是新列表还是少一个字母的旧列表"。
    void flushSearch();

signals:
    void hotkeyChanged(const QString& sequence);

protected:
    void paintEvent(QPaintEvent* e) override;
    bool event(QEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    void buildUi();
    void scheduleSearch();
    void runSearch();
    void startFileSearch(const QString& q);   // 深索引搜索放到线程池，别卡输入
    void rebuildList();
    void addSection(const QString& text);
    void addItem(const SearchResult& r);
    void activateRow(int row);
    void activateCurrent();
    void moveCurrent(int dir);
    int firstActionableRow() const;

    void startScan(bool quiet, bool forceDeep);
    void watchAppDirs();        // 监听开始菜单/桌面：装了新软件马上进索引
    QString currentCommandTarget() const;   // 当前行若是命令条目，返回它的 target
    bool pasteClipToPrevious();             // Ctrl+Enter：放回剪贴板并粘给呼出前的窗口
    void handleInit(const QString& arg);
    void handlePath(const QString& rest);
    void handleReg(const QString& rest);

    void loadUsage();
    void saveUsage();
    void bumpUsage(const QString& lnkPath);
    void openContextMenu(const QPoint& pos);
    void updateHeight();

    QLineEdit* m_input = nullptr;
    QToolButton* m_settingsBtn = nullptr;
    QListWidget* m_list = nullptr;
    QLabel* m_hint = nullptr;
    QMenu* m_menu = nullptr;
    QTimer* m_debounce = nullptr;
    QFileSystemWatcher* m_appWatch = nullptr;   // 快捷方式目录变了就重扫应用索引
    QTimer* m_appRescan = nullptr;              // 单次防抖：一次安装会触发几十条目录变更
    QTimer* m_periodicScan = nullptr;           // index/refreshHours 承诺的定时重扫
    QTimer* m_captureHold = nullptr;            // F1 浮层开着时推迟收起，浮层一关就补收
    QElapsedTimer m_lastAutoScan;
    WId m_prevFg = 0;        // 启动器弹出前的前台窗口（剪贴板历史要粘回给它）
    quint64 m_searchSeq = 0;      // 每次搜索递增，用于丢弃过期的异步结果
    bool m_fileSearchBusy = false;
    bool m_fileSearchRerun = false;

    QVector<SearchResult> m_results;
    QVector<RegIssue> m_regIssues;
    QVector<AppEntry> m_apps;
    QHash<QString, DevItem> m_detectItems; // "id|label" -> 检测结果
    QSet<QString> m_detecting;
    QHash<QString, int> m_usage;           // lnk 路径 -> 使用次数
    int m_detectRun = 0;
    QAtomicInt m_scanning{0};
    QElapsedTimer m_showTime; // 用于忽略弹出瞬间的失焦，避免闪烁隐藏
    QElapsedTimer m_showLatency;   // 从按下呼出到画出来的分段计时（诊断"唤出慢"）
    bool m_paintLogged = false;
    bool m_warmed = false;
    bool m_showFavorites = false;    // 配置里勾了才在空查询时展开常用列表         // 预热只做一次
    bool m_settingsOpen = false;
    QPointer<QWidget> m_settingsDlg;   // 设置页开着时热键不再弹搜索框
};
