// AppsFolder 那道缝的 POSIX 这一份：这台平台上**没有"打包应用"这一类**，返回空。
//
// 不是偷懒：Linux 上的应用身份就是 .desktop 文件 + 可执行路径，没有"只有虚拟文件夹能列出、
// 身份是一串 AUMID"那种东西。所以这里空着是对的答案，不是没做完的洞 ——
// 上层（AppScanner）拿到的就是"这一类零颗"，索引照旧从 .desktop / 目录里长。
#include "platform/AppsFolder.h"

namespace Platform {

QVector<PackagedApp> packagedApps()
{
    return {};
}

} // namespace Platform
