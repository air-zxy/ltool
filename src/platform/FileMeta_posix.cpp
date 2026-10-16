#include "platform/FileMeta.h"

#include <QImage>

namespace Platform {

bool extractIconToPng(const QString& iconFile, int index, const QString& pngOut)
{
    Q_UNUSED(index)
    // POSIX 上没有"从 exe 里抠第 index 个图标"这件事：能当图读的（ico/png/svg/…）就存下来，
    // 读不出就 false。调用方（AppScanner 的图标缓存）本来就按 false 走"没图标"那条分支。
    QImage img(iconFile);
    return !img.isNull() && img.save(pngOut);
}

QString fileVersion(const QString& path)
{
    Q_UNUSED(path)
    return {};   // 普通文件没有版本资源。空串在调用方就是"读不到"，不是错误。
}

} // namespace Platform
