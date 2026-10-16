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
bool isDisabled(const QString& id);
QWidget* page(const QString& id, QWidget* parent, QString* err);   // 懒创建 + 缓存
LToolPlugin* plugin(const QString& id);

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
