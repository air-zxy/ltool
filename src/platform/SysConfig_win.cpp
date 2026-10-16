// 平台层第八道缝，Windows 这一份：系统代理读 WinHTTP 那份 IE 设置，安装根按盘符判可用性。
// 两颗都是从 src/DevEnv.cpp 原样搬过来的（搬家前后的对照量在 docs/notes.md 那节）。
#include "platform/SysConfig.h"

#include <QDir>

#include <windows.h>
#include <winhttp.h>

namespace Platform {

// Windows 系统代理（设置里那个静态代理）。PAC/自动检测这里解不了，只能忽略。
QString systemProxyUrl()
{
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG pc = {};
    if (!WinHttpGetIEProxyConfigForCurrentUser(&pc))
        return {};
    QString raw = pc.lpszProxy ? QString::fromWCharArray(pc.lpszProxy) : QString();
    GlobalFree(pc.lpszAutoConfigUrl);
    GlobalFree(pc.lpszProxy);
    GlobalFree(pc.lpszProxyBypass);
    raw = raw.trimmed();
    if (raw.isEmpty())
        return {};
    // 可能写成 "http=127.0.0.1:10808;https=127.0.0.1:10808"，也可能直接是 "host:port"
    QString host;
    for (const QString& part : raw.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const int eq = part.indexOf(QLatin1Char('='));
        const QString key = eq < 0 ? QString() : part.left(eq).trimmed().toLower();
        const QString val = (eq < 0 ? part : part.mid(eq + 1)).trimmed();
        if (val.isEmpty() || key == QLatin1String("ftp") || key == QLatin1String("socks"))
            continue;
        if (key.isEmpty() || key == QLatin1String("https") || key == QLatin1String("http")) {
            host = val;
            break;
        }
    }
    if (host.isEmpty())
        host = raw;
    return normalizeProxy(host);
}

QString defaultInstallRoot()
{
    return QStringLiteral("D:\\software");
}

QString usableInstallRoot(const QString& configured)
{
    const QString root = configured.trimmed();
    // 留空是"不指定 --location"（装到软件自己默认的地方），不是"用默认根"—— 语义照旧，别改
    if (root.isEmpty())
        return root;
    // 配置的盘符在这台机器上不存在（换机器、没插盘、虚拟机）→ 同一路径退到 C 盘：
    // D:\software → C:\software，E:\tools → C:\tools
    if (root.size() >= 3 && root.at(1) == QLatin1Char(':')
        && root.at(0) != QLatin1Char('C') && !root.startsWith(QStringLiteral("\\\\"))) {
        const QString drive = QString(root.left(2) + QLatin1Char('\\'));
        if (!QDir(drive).exists())
            return QLatin1Char('C') + root.mid(1);
    }
    return root;
}

} // namespace Platform
