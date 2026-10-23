#pragma once

#include <QPointer>
#include <QString>
#include <QVector>
#include <QWidget>

#include "PluginStore.h"     // 一张列表：每张卡片就是一个条目，带状态和那颗按钮

class QLabel;
class QLineEdit;
class QKeyEvent;
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

    // 「本机装着这个 dll 但它起不来」那行卡片文案怎么拼。抽成静态纯函数是因为这一支
    // 只有装载真失败过才走得到，判据造不出一颗坏 dll —— 那就把**拼法**本身钉住：
    // 三个来源都给不出原因时必须落到兜底那句，绝不允许留一个以冒号结尾的空原因。
    // 2026-10-20 他问"注册表分析和开机分析为什么点不开"，那两张卡上写的正是
    // "已安装 · 本机有这个 dll 但没通过检查："—— 冒号后面是空的。
    static QString cannotOpenText(const QString& stateWord, const QString& loadErr,
                                  const QString& metaErr);

    void rebuildGrid();      // 重扫目录之后重画那一面卡片（装完/禁用完走的就是这条）    // 答了「是」之后那一段：删文件 / 排队 / 重扫 + 重排，答复写进卡片的状态标签。
    // 公开是为了离屏判据能跑同一条（无窗口平台上那个模态确认框会立刻返回「不」）。
    void applyUninstall(const QString& file, const QString& name, QLabel* state);
    void refreshStore();     // 联网拉清单：判哈希/落盘/改名都在 PluginStore 那几个纯函数里
    // 商店页那一行"上次自动更新说了什么"（2026-10-21 加自动更新时立的）。
    // 抽出来是因为它是**常驻**说明，不是某次动作的临时状态：卡片墙重排、拉清单失败都得让它还在。
    // static：判据要能不造一整扇商店页就问这一行说了什么（它不读任何成员）。
    static QString autoUpdateLine();

    // 「按名称筛这一面墙」那颗谓词：**只比名字**，不比描述、也不比关键词。
    // 为什么是静态纯函数：这一条规则要逐条钉（大小写、中文、首尾空格、空查询=全通过、
    // 关键词不该命中），而界面上每一次筛都读它 —— 筛的规则不许有第二份实现。
    static bool nameMatches(const QString& name, const QString& query);
    QString filterText() const;   // 过滤框现在是什么（判据读这一颗，不读控件像素）
    QString filterNote() const;   // 那一行"筛出几 / 共几"说了什么
    PluginStore::Shelf shelf() const { return m_shelf; }   // 现在这一档（全部/已装/未装）

private:
    QVector<PluginStore::Row> shopRows() const;   // 这一面卡片的数据，唯一一条路
    QWidget* buildGrid();
    void finishInstalls();   // 一批安装都落地之后只做一次的 重扫 + 重排
    QWidget* makeCard(const PluginStore::Row& r);
    QString reinstallHint(const PluginStore::Row& r) const;   // 「卸了还能装回来吗」那句实话
    void installEntry(const PluginStore::Entry& e, QPushButton* btn, QLabel* state);
    // 一次只让一颗 dll 在飞：他那一晚连点九颗，九条下载 + 九次改名 + 中途重扫叠在一起，
    // 崩在 scan() 里（Qt6Widgets，0xc0000005）。排队把这一类整个拿掉，顺带也不至于把镜像打爆。
    // 队列里存的也是卡片上的控件 —— 和安装回调同一个道理，必须用 QPointer 兜着：
    // 排着队的时候他可能点了「刷新清单」或者上一颗装完触发了重排，卡片就没了。
    struct Queued {
        PluginStore::Entry entry;
        QPointer<QPushButton> btn;
        QPointer<QLabel> state;
    };
    void runInstall(const PluginStore::Entry& e, QPushButton* btn, QLabel* state);   // 真开一条下载
    void startNextInstall();   // 队列里还有就接着装，装完了才重排卡片墙
    void backToGrid();         // 回到那一面卡片墙：按钮 / Esc / 重排之后共用这一条

    // 过滤框改字时走这一条：正看着一张插件页的话**不打断**（先记下，返回商店才重排那一面墙），
    // 在墙上的当场重排。绕开这一层的写法（直接 rebuildGrid）会把人从点开的那张页里拽出来。
    void applyFilter(const QString& text);
    void applyShelf(PluginStore::Shelf s);   // 换"全部/已装/未装"那一档：和 applyFilter 同一个规矩
    void updateFilterNote(int shown, int total, bool pending);
    // 两维（名字 ∧ 安装状态）"与"起来只在这里问一次：那一面墙用它筛，
    // 改了但还没重排时那句"挡住几张"也用它数 —— 两处各筛一遍迟早会给出不同的数。
    QVector<PluginStore::Row> passedRows(const QVector<PluginStore::Row>& rows) const;
    bool passesShelf(const PluginStore::Row& r) const;
    QString filterDesc() const;   // 现在这套过滤的人话（"名字「x」 · 只看已装"）

    QLineEdit* m_filter = nullptr;
    QLabel* m_filterNote = nullptr;
    QPushButton* m_shelfBtn[3] = {};   // 按 PluginStore::Shelf 的下标摆：全部 / 已装 / 未装
    QString m_filterText;      // 过滤框里现在那句（唯一一份：buildGrid 读它，不读控件）
    PluginStore::Shelf m_shelf = PluginStore::Shelf::All;
    bool m_filterDirty = false;   // 记着"改了过滤但还没重排"（正看着一张页时的那一段）

protected:
    void keyPressEvent(QKeyEvent* e) override;   // Esc = 从点开的那张页回到卡片墙

    QLabel* m_storeStatus = nullptr;
    QLabel* m_autoNote = nullptr;   // 「上次自动更新说了什么」那一行（常驻）
    bool m_storeBusy = false;
    QVector<Queued> m_queue;       // 点下去但还没开始的那些
    int m_installing = 0;    // 在飞的 dll 颗数；归零才 rebuildGrid（一颗装完就拆墙 = 把还在下的卡片控件删掉）
    // 存的是**清单本身**而不是画出来的那屏卡：rebuildGrid 整格换掉之后拿它重新比一次本地目录，
    // 刚装完/刚禁用完，状态就跟着目录里的文件动。
    QVector<PluginStore::Entry> m_lastEntries;
    QStringList m_lastProblems;
    bool m_haveStore = false;

    QStackedWidget* m_stack = nullptr;
    QToolButton* m_back = nullptr;
    QLabel* m_heading = nullptr;
};
