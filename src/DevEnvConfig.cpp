// DevEnv 里"动作两边共用、只有读的东西按平台不同"那四颗：代理怎么生效、装东西的根在哪。
//
// 为什么单独成一份 TU 而不是留在 src/DevEnv.cpp：src/DownloadManager.cpp 和
// src/PluginStore.cpp 已经在可移植名单里，链接期要在这儿找到 DevEnv::applyProxy 和
// DevEnv::installLocation；而 DevEnv.cpp 整个 TU 还卡在注册表 / 服务那套 Win32 上。
// 平台那半（系统代理从哪读、根目录长什么样）在 platform/SysConfig_{win,posix}.cpp。
//
// DevEnv::systemProxyUrl 只剩一层转接，为什么不干脆删掉：配置页那句"系统代理是 X"还在读
// 这个名字，而且 core 的导出面 = 已发布插件的 ABI —— 名字可以加，不可以缩（见 #91）。
#include "DevEnv.h"

#include "Utils.h"
#include "platform/SysConfig.h"

#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QSettings>
#include <QUrl>

namespace DevEnv {

QString systemProxyUrl()
{
    return Platform::systemProxyUrl();
}

QString proxyUrl()
{
    QSettings cfg(Utils::dataDir() + "/config.ini", QSettings::IniFormat);
    const QString manual = Platform::normalizeProxy(cfg.value("net/proxy").toString());
    if (!manual.isEmpty())
        return manual;
    return Platform::systemProxyUrl();
}

void applyProxy(QNetworkAccessManager* nam)
{
    if (!nam)
        return;
    const QString px = proxyUrl();
    if (px.isEmpty()) {
        nam->setProxy(QNetworkProxy::NoProxy);
        return;
    }
    const QUrl u(px);
    if (!u.isValid() || u.host().isEmpty())
        return;
    nam->setProxy(QNetworkProxy(QNetworkProxy::HttpProxy, u.host(),
                                quint16(u.port(u.scheme() == QLatin1String("https") ? 443 : 80))));
}

QString installLocation()
{
    QSettings cfg(Utils::dataDir() + "/config.ini", QSettings::IniFormat);
    // 「没配过」和「配了但留空」是两件事：前者用平台默认的根，后者是"不指定 --location"。
    // 这条语义在 Windows 上是有用户的（配置页那个安装目录清空 = 让安装器自己决定），别在这儿改掉。
    const QString configured = cfg.contains("install/path")
                                   ? cfg.value("install/path").toString()
                                   : Platform::defaultInstallRoot();
    return Platform::usableInstallRoot(configured);
}

} // namespace DevEnv
