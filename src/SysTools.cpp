#include "SysTools.h"

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

} // namespace SysTools
