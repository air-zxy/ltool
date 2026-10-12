#pragma once

#include <QString>
#include <QStringList>
#include <QWidget>

class QSettings;

// 主程序和插件之间唯一的契约。插件那边要带走的三样东西：这份头、`src/` 里它想复用的那些
// 头文件、以及 `ltoolcore.lib`（导入库）—— 实现只有一份，在 `ltoolcore.dll` 里，运行时它必须
// 和 ltool.exe 待在同一个目录（缺了它整个程序起不来，不是某个功能坏）。
//
// 只过 Qt 类型，不过 STL：std::string / std::vector 的布局跟编译开关走（/MT vs /MD、
// _ITERATOR_DEBUG_LEVEL），跨模块递过去等于拿两边的堆互相释放。Qt 的类型能安全跨边界，
// 前提是两边用的是**同一份 Qt6*.dll**（都按 /MD 链）—— 这也是 DLL 插件必须凑齐
// 同一个 MSVC 工具集 + 同一个 Qt 版本的原因，写在这里免得下一个人再猜一遍。
//
// 版本闸：iid 变了 QPluginLoader 自己就拒；apiVersion() 不等则主程序拒绝用这个插件，
// 并且把话讲明白（"这个插件是给 ltool API 2 编的"），别让人对着"加载失败"猜。
//
// API 2（2026-10-12）：加了 feedText —— 搜索框/剪贴板把一段文本递给某个工具
// （原来那是 JsonTool::open(text) / Base64Tool::open(text) 那条路，工具搬进插件之后
//  主程序不再认识那两个类，只能走接口）。
#define LTOOL_PLUGIN_API_VERSION 2
#define LTOOL_PLUGIN_IID "com.mateoki.ltool.plugin/2.0"

struct LToolPlugin
{
    virtual ~LToolPlugin() = default;

    virtual int apiVersion() const = 0;
    virtual QString id() const = 0;            // 小写、唯一，同时是配置前缀
    virtual QString name() const = 0;          // 界面上显示的那个名字
    virtual QString desc() const { return {}; }
    virtual QStringList keywords() const { return {}; }   // 搜索框命中词

    // 点开才调（和 ToolsPage 内嵌页同一套懒创建）；parent 由主程序给，主题/调色板也是主程序铺好的
    virtual QWidget* createPage(QWidget* parent) = 0;

    // 把一段文本交给这个工具（"在 JSON 工具里打开这段"）。页面早建好了缓存着，
    // 所以这里是"喂给已有那张页"。接住了返回 true；返回 false 时主程序照旧把页面打开，
    // 只是那段文本没进去 —— 不能因为接不住就把工具打不开。
    virtual bool feedText(const QString& text) { Q_UNUSED(text); return false; }

    // 配置：主程序给一个已经 setProgress 到本插件前缀的 QSettings，插件只管自己的那几键
    virtual void save(QSettings&) const {}
    virtual void load(const QSettings&) {}
};

Q_DECLARE_INTERFACE(LToolPlugin, LTOOL_PLUGIN_IID)
