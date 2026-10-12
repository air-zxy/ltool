#pragma once

#include <QColor>
#include <QString>
#include <QVector>

#include "ltool-plugin.h"

// DLL 插件的宿主：扫 `<exe 目录>/ltool-plugins/*.dll`，先只读元数据（不实例化，不占启动时间），
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
    QStringList keywords;
    QColor color;         // plugin.json 里的 "Color"（r,g,b），卡片图标用；没写就按 id 派生
    int api = 0;
    QString error;        // 非空 = 这个文件没通过检查，界面上要说人话
    bool ok() const { return error.isEmpty(); }
};

QString pluginsDir();                 // <exe 目录>/ltool-plugins
const QVector<Meta>& scan();          // 只读元数据；结果缓存，reload() 才重扫
void reload();
const Meta* meta(const QString& id);
QWidget* page(const QString& id, QWidget* parent, QString* err);   // 懒创建 + 缓存
LToolPlugin* plugin(const QString& id);

// 把一段文本递给那个工具的页（搜索框/剪贴板的"在工具里打开这段"）。
// 页早建好缓存着，所以这里只是转手；插件没接住返回 false，调用方照常把页面打开。
bool feed(const QString& id, const QString& text);

// 插件自己带的判据（extern "C" int ltoolPluginSelftest()），没有返回 nullptr。
// 页面代码搬进插件之后，主程序链接不到它内部的测试函数，只能这样点名要。
void* selftestEntry(const QString& id);

} // namespace PluginHost
