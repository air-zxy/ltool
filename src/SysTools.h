#pragma once

#include <QVector>

#include "AppScanner.h"

// Windows 内置面板/工具（控制面板、MSC 管理单元、系统 exe、ms-settings 设置页）。
// 以 AppEntry 形式提供，category = "system"，仅供内存索引（不写入 apps.json）。
namespace SysTools {

QVector<AppEntry> systemTools();

// 电源那两件事：搜索框里整条输入「休眠 / sleep」「关机 / shutdown」时回车直接动手。
// 返回空串 = 已经叫起来了；非空 = 人话原因（没开休眠、权限不够…）—— 调用方必须照说，
// 不许把失败咽下去（"按了没反应"是这程序里最难查的形状）。
QString hibernateNow();
QString shutDownNow();
// 这台机器现在**能不能**休眠（固件支持且系统里已启用）。搜索框拿它决定那一行怎么说：
// 没开就别等人按下才报错，把「powercfg /h on」直接写在说明里
// （2026-10-19 这台机器实测：`powercfg /a` 报「休眠：尚未启用休眠」）。
bool hibernationEnabled();

} // namespace SysTools
