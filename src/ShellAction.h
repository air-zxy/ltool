#pragma once

#include <QString>

// 资源管理器右键菜单点进来之后，由这个进程负责弹界面。
// flag 是 ShellMenu 写进注册表的那个开关（--shred / --fileinfo / --whohas …）。
namespace ShellAction {

int run(const QString& flag, const QString& path);

} // namespace ShellAction
