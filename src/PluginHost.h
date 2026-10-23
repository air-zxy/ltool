#pragma once

#include <QColor>
#include <QString>
#include <QVector>

#include "ltool-plugin.h"

// DLL 插件的宿主：扫 `<exe 目录>/ltool-plugins/` 里按平台命名的那一族产物
// （Windows `ltool-<id>.dll`，Linux `libltool-<id>.so`，macOS `.dylib` —— 规则在
// platform/PluginNames.h，装载本身用 QPluginLoader/QLibrary，不需要平台缝），
// 先只读元数据（不实例化，不占启动时间），
// 点开某张卡片时才 createInstance + createPage，并把那个页面缓存下来复用。
//
// 故意**不卸载**：Qt 插件的 dll 一旦 unload，里面还活着的 QWidget/定时器就是悬空指针，
// 而 ltool 的插件页是挂在设置窗里的，生命周期不好保证。所以插件只进不出，
// 想禁用就改名/删文件（下次启动生效），并在界面上把这条写明白。
namespace PluginHost {

struct Meta {
    QString file;
    QString id;
    QString name;
    QString desc;
    QString version;
    QStringList keywords;
    QStringList actions;  // plugin.json 里 "Actions"：这个插件声明它接哪些动作（商店/诊断用）
    bool wantsActivate = false;  // plugin.json 里 "Activate":true —— 它要在启动时就被叫起来干活
    QColor color;         // plugin.json 里的 "Color"（r,g,b），卡片图标用；没写就按 id 派生
    int api = 0;
    QString error;        // 非空 = 这个文件没通过检查，界面上要说人话
    bool ok() const { return error.isEmpty(); }
};

QString pluginsDir();                 // <exe 目录>/ltool-plugins
const QVector<Meta>& scan();          // 只读元数据；结果缓存，reload() 才重扫
void reload();
const Meta* meta(const QString& id);
// 问某个**文件**自己声明的 Version（读嵌在产物里那份 plugin.json，不实例化、不装载）。
// 商店那侧要判"清单这份是不是比本机新"就得出这个数；读不到返回空串，调用方宁可说"说不清"。
QString metaVersionOf(const QString& path);
bool isDisabled(const QString& id);
QWidget* page(const QString& id, QWidget* parent, QString* err);   // 懒创建 + 缓存
LToolPlugin* plugin(const QString& id);

// ---- 建页那一条边界的 SEH 外壳（2026-10-23）----
// 插件里读/写坏内存（`0xc0000005` 那一类 —— 2026-10-22 那次「b64 会闪退」就是这一类）
// 不该把整个程序一起带走。**跨进插件代码只在这一颗**，判据也走这一颗
// （拿一颗故意崩的接口进去 ⇒ 谁把这层外壳从 `page()` 里摘掉，这一趟就当场死给它看）。
// 买到的是什么要说清：**窗口不消失 + 异常码和地址进日志**，不是"这个 bug 被关住了" ——
// 掉进访问违例时那一趟已经碰过坏内存，后面是带着未知损伤继续跑；
// 半建出来的那张页我们不当成好页缓存，界面上照实说一句（`ToolsPage` 那一行本来就会说原因）。
struct SehHit {
    unsigned long code = 0;     // 异常码：0xC0000005 = 访问违例，0xC00000FD = 栈溢出
    void* addr = nullptr;       // 落在哪条指令上（能喂 pe_rva.py 去问它旁边是哪个导出）
};
// 真的进插件那一趟；返回 nullptr = 没给出页（撞了异常，或它自己返回空），原因写进 err。
QWidget* buildPage(LToolPlugin* p, const QString& id, QWidget* parent, QString* err);

// ---- 剩下几条边界用的是同一层外壳（2026-10-23 下午，接 `buildPage` 那条往后铺）----
// 规矩和建页那条一样：**每条边界只有一个真的跨进插件的函数**，`feed()` / `invoke()` /
// `startBackground()` / `plugin()` 只做"找到那颗接口，再交给这一颗"；判据调的也是这同一颗函数，
// 不是另写一个近似的外壳 —— 谁把调用从这层里挪出去，判据就当场死给它看。
// 返回值说的是**这一趟回没回来**（false = 撞在异常里，`why` 是给人看的那句话），
// 插件自己答"是/否"走 `accepted` / `didIt` —— 两种"否"必须分得开：
// 界面上"它接了但没干成"和"它崩了"是两句话。`why` 也照 `buildPage` 的规矩：进来先 clear，
// 回得来就不留原因（不清的话调用方复用同一个 QString 会把上一趟的失败念成这一趟的）。
bool feedInto(LToolPlugin* p, const QString& id, const QString& text, bool* accepted, QString* why);
bool invokeInto(LToolPlugin* p, const QString& id, const QString& action, const QStringList& args,
                bool* didIt, QString* why);
// 后台那颗挂的是 extern "C" 的 `ltoolPluginActivate`（不是虚函数 —— 动 vtable 等于把 API 推到 5）。
// `startBackground()` 唯一的活儿就是把这个函数指针递进来。
bool activateInto(const QString& id, void (*fn)(), QString* why);
// 装载那一步要问实例自己报的 API（10-22 那批"名字对得上、一用就崩"就死在这一类调用上），
// 和路由前那句"你接不接这个动作" —— 这两颗也是跨边界的，一并走同一层壳。
bool apiVersionInto(LToolPlugin* p, const QString& id, int* api, QString* why);
bool actionsInto(LToolPlugin* p, const QString& id, QStringList* actions, QString* why);

// 把一段文本递给那个工具的页（搜索框/剪贴板的"在工具里打开这段"）。
// 页早建好缓存着，所以这里只是转手；插件没接住返回 false，调用方照常把页面打开。
bool feed(const QString& id, const QString& text);

// 动作路由（API 3）：热键/右键菜单这些入口留在主程序，按下之后把动作名+参数转给
// **声明了接这个动作的插件**。谁(action, who) 回哪个插件接的（没找到时给你写日志用）。
// 找不到 = 返回 false 并且把话讲明白，调用方必须让界面/托盘说实话，
// 不能静默 —— "按了 F1 什么都没发生"最难查，就是因为下面的人静默吞了。
bool invoke(const QString& action, const QStringList& args, QString* who = nullptr);
// 哪个插件接这个动作（不装载，只看元数据声明；没有返回空）。界面上"这个功能由 X 提供"用它。
QString actionOwner(const QString& action);
// 那颗插件最近一次**装载失败**的原因（起来了就是空串）。
QString loadErrorOf(const QString& id);
// "这个动作没人接"那句实话怎么写：分得清"根本没装"和"装了但起不来（附原因）"。
// 前提是先调过 `invoke()` —— 装载记录是那时留下的。
QString whyNoOwner(const QString& action);

// 插件自己带的判据入口（extern "C" 导出），没有返回 nullptr。
// 页面代码搬进插件之后主程序链接不到它内部的测试函数，只能按名字点名要。
// 默认那个名字是快判据（跟着 --plugintest 一起跑）；慢的（要扫一整盘的）另起一个名字，
// 免得把六套回归全拖成几十秒。
void* entry(const QString& id, const char* symbol = "ltoolPluginSelftest");

// 启动时把**挂了牌**的插件叫起来：plugin.json 里 "Activate":true 的那些，宿主按名字找它
// 导出的 "ltoolPluginActivate"（extern "C"，不需要实例化 Qt 插件对象 —— 那等于把懒创建
// 整个废掉），找到就叫。没挂牌的一个 dll 都不多装。返回叫起来几个，调用方只用来写日志。
// 为什么不走接口虚函数：加虚函数会动 vtable，等于把 API 版本推进 4，
// 而按 API 不等就拒装的规矩会把用户装好的每一颗插件全挡在门外。
// 没导出那个符号却挂了牌 = 启动时点名报错（界面/日志都得说实话）。
int startBackground();
inline void* selftestEntry(const QString& id) { return entry(id); }

} // namespace PluginHost
