#include "SysTools.h"

#include "Utils.h"
#include "platform/Launch.h"

#include <QProcess>

#ifdef _WIN32
#include <windows.h>
#include <powrprof.h>   // CallNtPowerInformation(SystemPowerCapabilities) —— 休眠开没开
#endif

namespace {

// 一条系统面板/工具：target 直接交给 ShellExecute，所以 .cpl/.msc/ms-settings:/裸 exe 都行。
// 名字必须是中文 —— 搜索框那侧靠拼音匹配（"renwuguanliqi"/"rwglq" 要能命中"任务管理器"）。
//
// ⚠️ 这份清单是 2026-10-12 重建的：原文件在事故里丢了，名字和条目按 Windows 上
// 本来就有的那些面板重列了一遍，和你以前手调过的那份**不一定一模一样**，请过一眼删减。
AppEntry tool(const QString& name, const QString& target, const QString& args = {})
{
    AppEntry e;
    e.name = name;
    e.target = target;
    e.args = args;
    e.category = "system";
    return e;
}

} // namespace

namespace SysTools {

QVector<AppEntry> systemTools()
{
    QVector<AppEntry> v;
    // 设置页（ms-settings: 是 Windows 10/11 自带的设置应用）
    v << tool(QStringLiteral("设置"), QStringLiteral("ms-settings:"))
      << tool(QStringLiteral("系统信息"), QStringLiteral("ms-settings:about"))
      << tool(QStringLiteral("显示设置"), QStringLiteral("ms-settings:display"))
      << tool(QStringLiteral("声音设置"), QStringLiteral("ms-settings:sound"))
      << tool(QStringLiteral("通知设置"), QStringLiteral("ms-settings:notifications"))
      << tool(QStringLiteral("应用和功能"), QStringLiteral("ms-settings:appsfeatures"))
      << tool(QStringLiteral("默认应用"), QStringLiteral("ms-settings:defaultapps"))
      << tool(QStringLiteral("启动项设置"), QStringLiteral("ms-settings:startupapps"))
      << tool(QStringLiteral("磁盘清理设置"), QStringLiteral("ms-settings:storagesense"))
      << tool(QStringLiteral("网络设置"), QStringLiteral("ms-settings:network"))
      << tool(QStringLiteral("Windows 更新"), QStringLiteral("ms-settings:windowsupdate"))
      << tool(QStringLiteral("隐私设置"), QStringLiteral("ms-settings:privacy"))
      << tool(QStringLiteral("日期和时间"), QStringLiteral("ms-settings:dateandtime"));

    // 传统控制面板小程序
    v << tool(QStringLiteral("控制面板"), QStringLiteral("control.exe"))
      << tool(QStringLiteral("程序和功能"), QStringLiteral("appwiz.cpl"))
      << tool(QStringLiteral("系统属性"), QStringLiteral("sysdm.cpl"))
      << tool(QStringLiteral("显示设置"), QStringLiteral("desk.cpl"))
      << tool(QStringLiteral("电源选项"), QStringLiteral("powercfg.cpl"))
      << tool(QStringLiteral("网络连接"), QStringLiteral("ncpa.cpl"))
      << tool(QStringLiteral("鼠标设置"), QStringLiteral("main.cpl"))
      << tool(QStringLiteral("用户账户"), QStringLiteral("nusrmgr.cpl"));

    // 管理单元与系统 exe（这些要多数一次管理员提权，ShellExecute 会照常弹 UAC）
    v << tool(QStringLiteral("任务管理器"), QStringLiteral("taskmgr.exe"))
      << tool(QStringLiteral("服务"), QStringLiteral("services.msc"))
      << tool(QStringLiteral("设备管理器"), QStringLiteral("devmgmt.msc"))
      << tool(QStringLiteral("注册表编辑器"), QStringLiteral("regedit.exe"))
      << tool(QStringLiteral("事件查看器"), QStringLiteral("eventvwr.msc"))
      << tool(QStringLiteral("本地安全策略"), QStringLiteral("secpol.msc"))
      << tool(QStringLiteral("任务计划程序"), QStringLiteral("taskschd.msc"))
      << tool(QStringLiteral("磁盘管理"), QStringLiteral("diskmgmt.msc"))
      << tool(QStringLiteral("组件服务"), QStringLiteral("comexp.msc"))
      << tool(QStringLiteral("系统配置"), QStringLiteral("msconfig.exe"))
      << tool(QStringLiteral("系统信息"), QStringLiteral("msinfo32.exe"))
      << tool(QStringLiteral("本地用户和组"), QStringLiteral("lusrmgr.msc"))
      << tool(QStringLiteral("防火墙高级安全"), QStringLiteral("wf.msc"))
      << tool(QStringLiteral("磁盘清理"), QStringLiteral("cleanmgr.exe"));

    return v;
}

namespace {

// 走系统自带的 shutdown.exe，而不是 SetSuspendState/ExitWindowsEx：那条要新链 powrprof、
// 还要自己拼特权，而 shutdown.exe 的报错是给人看的（"休眠功能未启用"这类），直接回给界面。
// QProcess + Platform::runWithoutConsole = 不闪黑窗；拿得到退出码和 stderr，失败就能说实话。
QString runShutdown(const QStringList& args)
{
    QProcess p;
    Platform::runWithoutConsole(&p);
    p.start(QStringLiteral("shutdown"), args);
    if (!p.waitForStarted(3000))
        return QStringLiteral("shutdown.exe 起不来：%1").arg(p.errorString());
    // 成功的话这台机器立刻就没了，等 3 秒只为拿一个"它当场就报错"的回话；超时当作已动手。
    if (!p.waitForFinished(3000))
        return QString();
    if (p.exitCode() == 0)
        return QString();
    const QString err = Utils::ConsoleLines::decode(p.readAllStandardError()).trimmed();
    return QStringLiteral("shutdown %1 没成（退出码 %2）%3")
        .arg(args.join(QLatin1Char(' ')))
        .arg(p.exitCode())
        .arg(err.isEmpty() ? QString() : QStringLiteral("：") + err);
}

} // namespace

QString hibernateNow()
{
    return runShutdown({ QStringLiteral("/h") });
}

QString shutDownNow()
{
    return runShutdown({ QStringLiteral("/s"), QStringLiteral("/t"), QStringLiteral("0") });
}

// 这颗 TU 在"非 Windows 也编得过"那份名单里（LTOOL_CORE_PORTABLE_SRC），所以 Win32 那截必须圈起来。
#ifdef _WIN32
bool hibernationEnabled()
{
    SYSTEM_POWER_CAPABILITIES caps{};
    if (CallNtPowerInformation(SystemPowerCapabilities, nullptr, 0, &caps, sizeof(caps)) != 0)
        return false;              // 问不到就按"不能"说，那一行照样给得出路（回车那次仍会报真原因）
    // 成员名按 SDK 那份（winnt.h 的 SYSTEM_POWER_CAPABILITIES）。**两格都要看**：
    // SystemS4 只是"固件支持 S4"，HiberFilePresent 才是"休眠文件在 = 休眠当前启用"。
    // 只查 SystemS4 会和 `powercfg /a` 打架 —— 这台机器实测 SystemS4=1 而 powercfg 说
    // 「休眠：尚未启用休眠」，第一版就因此写错了一行话（2026-10-19 复测抓出来的）。
    return caps.SystemS4 != 0 && caps.HiberFilePresent != 0;
}
#else
bool hibernationEnabled()
{
    return false;                  // 非 Windows 上没有"休眠"这个可叫的东西（#90 那一档）
}
#endif

} // namespace SysTools
