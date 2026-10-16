#pragma once

#include <QString>

// Windows 资源管理器的右键菜单集成：在 HKCU\Software\Classes 下写静态 shell 项，
// 让"文件 / 文件夹"右键里多一个 ltool 子菜单（解压 / 压缩 / 粉碎 / 占用分析 / 谁占用着它）。
//
// 只写 HKCU，不碰 HKLM —— 不需要管理员，也不会影响这台机器上的其他用户。
// 默认关闭：配置页里勾上才会写入，取消勾选会把这些键删干净。
namespace ShellMenu {

// 已经集成过了吗（看 HKCU 下我们的根键在不在）
bool installed();

// 写入 / 撤销。返回一句给人看的结论（失败时会带上 Windows 的错误码）
QString install();
QString remove();

// 只在"该有"的时候用：已经集成过、但表里新加了项或删了项（老机器的右键不会自己变）——
// 发现对不上就整表重写一遍（install 本身是幂等的）。启动时由 syncWithPlugin() 顺带调用。
void refreshIfStale();

// 给配置页显示"会往哪儿写"
QString locationHint();

// 右键那几项的"该不该在"是两件事的与：**用户想不想要**（配置里那个勾，落盘在 shell/intent）
// 和**提供者在不在了**（「右键动作」那个插件装着且没禁用）。
// 卸载了插件就要从右键里消失 —— 静态注册表项不会自己判断，所以得有人按这个规则写/删。
bool providerPresent();                 // plugins/<shell> 在且没被改名 .off
bool intent();                          // 用户那个勾（不是注册表现状）
void setIntent(bool on);
inline bool menuShouldBe(bool intent, bool provider) { return intent && provider; }

// 按上面那条规则把注册表对齐（该有而没有就写，不该有却还在就删干净）。
// 启动时、以及插件装了/卸了/禁用/启用之后各调一次。回一句给人看的话（没动就回空串）。
QString syncWithPlugin();

} // namespace ShellMenu
