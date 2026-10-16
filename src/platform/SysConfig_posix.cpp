// 平台层第八道缝，POSIX 这一份：系统代理读那一族环境变量，安装根落在家目录下面。
//
// 两边机制本来就不同，这不是把 Windows 那套降级：
//  · 代理 —— Linux 桌面环境没有"IE 设置"那份注册表可读，标准做法就是
//    `http_proxy` / `https_proxy` 这一族环境变量（curl、wget、pip、git 全认它）。
//    已知边界两条，和 Windows 那半对称：① PAC / 自动发现脚本解不了（Windows 也解不了）；
//    ② `socks://` / `socks5://` 这类值这里**不当代理返回**，因为下游那颗
//    `DevEnv::applyProxy` 只会摆 QNetworkProxy::HttpProxy，把它报成 http 代理是骗人。
//    所以 socks 只有环境变量的话结果是"不用代理"，宁缺不假。
//  · 安装根 —— 没有盘符这回事。空、Windows 那种 "D:\software" 写法（配置文件是从
//    Windows 那边同步过来的）、以及相对路径，一律退到 ~/software；理由都是同一个：
//    装东西的根必须在这台机器上真的能用，不能跟着工作目录跑。
#include "platform/SysConfig.h"

#include <QDir>

#include <cstdlib>

namespace Platform {

namespace {

const char* const kProxyVars[] = { "http_proxy", "HTTP_PROXY", "https_proxy", "HTTPS_PROXY" };

// socks 那族直接丢：下面 applyProxy 只会摆 HttpProxy，报成 http 代理是假话
bool usableProxyValue(const QString& v)
{
    const QString lower = v.trimmed().toLower();
    if (lower.isEmpty())
        return false;
    return !lower.startsWith(QLatin1String("socks"));
}

} // namespace

QString systemProxyUrl()
{
    for (const char* name : kProxyVars) {
        const QByteArray raw = qgetenv(name);
        if (raw.isEmpty())
            continue;
        const QString value = QString::fromLocal8Bit(raw).trimmed();
        if (!usableProxyValue(value))
            continue;
        return normalizeProxy(value);
    }
    return {};
}

QString defaultInstallRoot()
{
    return QDir::homePath() + QStringLiteral("/software");
}

QString usableInstallRoot(const QString& configured)
{
    const QString root = configured.trimmed();
    if (root.isEmpty())
        return defaultInstallRoot();         // POSIX 上没有"不指定装哪"这回事，给一个真能用的
    if (root.size() >= 2 && root.at(1) == QLatin1Char(':'))
        return defaultInstallRoot();         // "D:\software"：那是 Windows 的写法，不是这里的路径
    if (!root.startsWith(QLatin1Char('/')))
        return defaultInstallRoot();         // 相对路径会跟着工作目录跑，装东西不能这样
    return root;
}

} // namespace Platform
