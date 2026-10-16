#pragma once

#include <QtGlobal>
#include <QString>
#include <QStringList>

// 平台层第六道缝：插件产物的**名字规则**。
// 装载那一路不需要缝 —— PluginHost 用的是 QPluginLoader / QLibrary，Qt 自己就跨平台。
// （这一刀原本的规格写着"这里有三处 Win32 调用要收掉"，通读 288 行实测一处都没有：
//  真正的裸 LoadLibrary/GetProcAddress 在 plugins/capture/OcrPp.cpp，那是 onnxruntime
//  的句柄，跟插件宿主无关。硬包一层 Platform::PluginLoad 就是把 Qt 的实现平行做一遍。）
// 剩下的平台差异只有文件名：
//   Windows  ltool-<id>.dll
//   Linux    libltool-<id>.so      POSIX 的加载器认 lib 前缀，不带就找不到这个 soname
//   macOS    libltool-<id>.dylib
// 为什么写成头文件内联、不做 _win/_posix 两份 .cpp：每条 OS 就一个常量，拆两份要多两个
// 翻译单元，还要跟着 core 的导出面走（#91 那道 ABI 闸管的就是这个）；名字规则不值那套机械。
// **不吃这套的是商店那一侧**：PluginStore 校验清单时写死「file 必须以 .dll 结尾」，
// plugin.json 里的 file 和清单里的 url 也都带 dll 字样 —— 那是 dist 协议要不要跟着变
// 的问题（任务 #78），不是这里能替它拍板的。
namespace Platform {

// 共享库后缀，不带点：Windows "dll" / Linux "so" / macOS "dylib"
inline QString pluginFileSuffix()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("dll");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("dylib");
#else
    return QStringLiteral("so");
#endif
}

// 扫插件目录时用的匹配式（QDir::entryList 要 QStringList）
inline QStringList pluginFileGlobs()
{
    return { QStringLiteral("*.") + pluginFileSuffix() };
}

// 那个 id 的插件文件名。Windows 侧算出来就是原来写死的 "ltool-" + id + ".dll"，
// 一个字符都没变；POSIX 侧前面还得挂上 lib，否则加载器按 soname 找不到。
inline QString pluginFileName(const QString& id)
{
#if defined(Q_OS_WIN)
    return QStringLiteral("ltool-") + id + QLatin1Char('.') + pluginFileSuffix();
#else
    return QStringLiteral("libltool-") + id + QLatin1Char('.') + pluginFileSuffix();
#endif
}

} // namespace Platform
