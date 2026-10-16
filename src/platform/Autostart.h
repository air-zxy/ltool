#pragma once

#include <QString>

// 平台层第三道缝：开机自启。这一道和前面两道不一样 —— 它不是"Windows 有、别的平台只能降级"，
// 三个平台各有各的机制，而且都是"注册一个登录时/桌面启动时要跑的东西"：
//   Windows  HKCU\Software\Microsoft\Windows\CurrentVersion\Run 里那一个值（不需要管理员）
//   Linux    ~/.config/autostart/ltool.desktop（XDG 自动启动规范，桌面环境登录时读它）
//   macOS    ~/Library/LaunchAgents/com.ltool.autostart.plist + RunAtLoad
//            （写这个文件只是"注册上"，真正装载要么等下次登录，要么 launchctl bootstrap ——
//             那是另一件事，这里不做，界面上也就不能说"已经生效了"）
namespace Platform {

void setAutostart(bool on);
bool autostartEnabled();

// 条目落在哪儿（Windows 是那个注册表键，POSIX 是那个文件）。暴露出来是为了能被判据直接问：
// 这条路一旦拼错，现象只有"勾了没反应"，没有任何别的可观测面。
QString autostartTarget();

// 那个条目该写成什么内容。纯函数，同样是为了能直接问 —— 特别是 Exec 那一行有没有真的
// 放进去当前这个 exe 的路径（挪过安装目录之后自启悄悄失效，就是这一行没跟着走）。
QString autostartEntry(const QString& exePath);

} // namespace Platform
