#pragma once

#include <QString>

// 平台层第八道缝：这台机器对两个系统级问题的答案 —— 代理在哪、东西该装在哪。
//
// 为什么缝开在这里而不是整颗 DevEnv::applyProxy：装代理那套动作（QNetworkProxy 怎么摆）
// 两边一样，不一样的只有"从哪儿读到系统那半"和"这个平台上根目录长什么样"。
// Windows 读 WinHTTP 那份 IE 设置（PAC / 自动发现解不了就返回空），POSIX 读
// http_proxy / https_proxy 那一族环境变量 —— 两个都是"这台机器的代理"，不是一个降级成一个。
namespace Platform {

// 把 "127.0.0.1:10808" 这类写法补成 winget / QNetworkAccessManager 认的 URL。
// 这条规矩两边共用，所以只写这一份（inline）：空、或者连冒号都没有的写法都当"没代理"。
inline QString normalizeProxy(QString p)
{
    p = p.trimmed();
    if (p.isEmpty() || !p.contains(QLatin1Char(':')))
        return {};
    if (!p.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
        && !p.startsWith(QLatin1String("https://"), Qt::CaseInsensitive))
        p = QLatin1String("http://") + p;
    return p;
}

// 系统层面的代理（不含用户在配置页里手填那一半）。读不到就返回空 = 不用代理，不许猜一个。
QString systemProxyUrl();

// 配置里**没写过**"安装目录"时，这个平台默认的根（Windows: D:\software；POSIX: ~/software）。
QString defaultInstallRoot();

// 配过的那个根在这台机器上能不能用：
//   Windows：盘符不在这台机器上（换机器/没插盘/虚拟机）就退到 C 盘同一个路径；
//            留空原样返回空 —— 界面上那条规矩是"留空 = 不指定 --location"，不许在这儿改语义。
//   POSIX：没有盘符这回事 —— 空、"D:\software" 那种 Windows 写法（配置是从那边同步过来的）、
//          以及相对路径，一律退到 defaultInstallRoot()，因为装东西的根必须真的能用。
QString usableInstallRoot(const QString& configured);

} // namespace Platform
